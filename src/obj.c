#include "common.h"
#include "error.h"
#include "obj.h"
#include "obj-internal.h"
#include "obj/dwarf.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>


static const int kCDSymtabInitialSize = 16384;

static cd_error_t cd_obj_count_segs(cd_obj_t* obj,
                                    cd_segment_t* seg,
                                    void* arg);
static cd_error_t cd_obj_fill_segs(cd_obj_t* obj,
                                   cd_segment_t* seg,
                                   void* arg);
static cd_error_t cd_obj_insert_seg_ends(cd_obj_t* obj,
                                         cd_segment_t* seg,
                                         void* arg);
static int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b);
static int cd_symbol_sort(const cd_sym_t* a, const cd_sym_t* b);
static cd_error_t cd_obj_init_syms(cd_obj_t* obj);
static cd_error_t cd_obj_insert_syms(cd_obj_t* obj,
                                     cd_sym_t* sym,
                                     void* arg);
static cd_error_t cd_obj_init_dwarf(cd_obj_t* obj);
static cd_error_t cd_obj_init_aslr(cd_obj_t* obj, cd_obj_opts_t* opts);


/* Wrappers around method */

cd_obj_t* cd_obj_new(cd_obj_method_t* method,
                     const char* path,
                     cd_error_t* err) {
  return cd_obj_new_ex(method, path, NULL, err);
}


cd_obj_t* cd_obj_new_ex(cd_obj_method_t* method,
                        const char* path,
                        cd_obj_opts_t* opts,
                        cd_error_t* err) {
  cd_obj_t* res;
  int fd;

  fd = open(path, O_RDONLY);
  if (fd == -1) {
    *err = cd_error_num(kCDErrFileNotFound, errno);
    return NULL;
  }

  res = method->obj_new(fd, opts, err);
  if (cd_is_ok(*err)) {
    res->method = method;
    res->fd = fd;
  } else {
    close(fd);
    return NULL;
  }

  res->path = path;

  if (opts != NULL && opts->parent != NULL) {
    *err = cd_obj_add_dso(opts->parent, res);
    if (!cd_is_ok(*err)) {
      cd_obj_free(res);
      return NULL;
    }
  }

  if (opts != NULL && opts->reloc != 0) {
    *err = cd_obj_init_aslr(res, opts);
    if (!cd_is_ok(*err)) {
      cd_obj_free(res);
      return NULL;
    }
  }

  return res;
}


void cd_obj_free(cd_obj_t* obj) {
  obj->method->obj_free(obj);
}


int cd_obj_is_x64(cd_obj_t* obj) {
  return obj->is_x64;
}


int cd_obj_is_core(cd_obj_t* obj) {
  return obj->method->obj_is_core(obj);
}


cd_error_t cd_obj_get_thread(cd_obj_t* obj,
                             unsigned int index,
                             cd_obj_thread_t* thread) {
  return obj->method->obj_get_thread(obj, index, thread);
}


cd_error_t cd_obj_iterate_syms(cd_obj_t* obj,
                               cd_obj_iterate_sym_cb cb,
                               void* arg) {
  return obj->method->obj_iterate_syms(obj, cb, arg);
}


cd_error_t cd_obj_get_dbg_frame(cd_obj_t* obj,
                                void** res,
                                uint64_t* size,
                                uint64_t* vmaddr) {
  return obj->method->obj_get_dbg_frame(obj, res, size, vmaddr);
}


cd_error_t cd_obj_iterate_segs(cd_obj_t* obj,
                               cd_obj_iterate_seg_cb cb,
                               void* arg) {
  return obj->method->obj_iterate_segs(obj, cb, arg);
}


/* Just a common implementation */


cd_error_t cd_obj_insert_syms(cd_obj_t* obj, cd_sym_t* sym, void* arg) {
  cd_sym_t* copy;
  uint64_t val;

  /* Skip empty symbols */
  if (sym->nlen == 0 || sym->value == 0)
    return cd_ok();

  val = sym->value + obj->aslr;

  if (cd_hashmap_insert(&obj->syms,
                        sym->name,
                        sym->nlen,
                        (void*) val) != 0) {
    return cd_error_str(kCDErrNoMem, "cd_hashmap_insert");
  }

  copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return cd_error_str(kCDErrNoMem, "cd_sym_t");
  *copy = *sym;

  /* ASLR slide value */
  copy->value = val;

  if (cd_splay_insert(&obj->sym_splay, copy) != 0)
    free(copy);

  return cd_ok();
}


cd_error_t cd_obj_insert_seg_ends(cd_obj_t* obj,
                                  cd_segment_t* seg,
                                  void* arg) {
  cd_sym_t* copy;

  copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return cd_error_str(kCDErrNoMem, "cd_sym_t");

  copy->name = NULL;
  copy->nlen = 0;
  copy->value = obj->aslr + seg->end;

  if (cd_splay_insert(&obj->sym_splay, copy) != 0)
    free(copy);

  return cd_ok();
}


cd_error_t cd_obj_init_syms(cd_obj_t* obj) {
  cd_error_t err;

  if (obj->has_syms)
    return cd_ok();

  cd_splay_init(&obj->sym_splay,
                (int (*)(const void*, const void*)) cd_symbol_sort);
  obj->sym_splay.allocated = 1;

  if (cd_hashmap_init(&obj->syms, kCDSymtabInitialSize, 0) != 0) {
    cd_splay_destroy(&obj->sym_splay);
    return cd_error_str(kCDErrNoMem, "cd_hashmap_t");
  }
  obj->has_syms = 1;

  if (cd_obj_is_core(obj))
    return cd_ok();

  /* Insert seg_ends first, to not let `_end` overwrite them on linux */
  err = cd_obj_iterate_segs((cd_obj_t*) obj,
                            cd_obj_insert_seg_ends,
                            NULL);
  if (!cd_is_ok(err))
    return err;

  return cd_obj_iterate_syms((cd_obj_t*) obj, cd_obj_insert_syms, NULL);
}


cd_error_t cd_obj_init_dwarf(cd_obj_t* obj) {
  cd_error_t err;
  void* dbg;
  uint64_t dbg_vmaddr;
  uint64_t dbg_size;

  if (obj->cfa != NULL)
    return cd_ok();

  err = cd_obj_get_dbg_frame(obj, &dbg, &dbg_size, &dbg_vmaddr);
  if (err.code == kCDErrNotFound)
    return cd_ok();
  if (!cd_is_ok(err))
    return err;

  err = cd_dwarf_parse_cfa(obj, dbg_vmaddr, dbg, dbg_size, &obj->cfa);
  if (!cd_is_ok(err))
    return err;

  return cd_ok();
}


cd_error_t cd_obj_get_sym(cd_obj_t* obj,
                          const char* sym,
                          uint64_t* addr) {
  cd_error_t err;
  void* res;
  QUEUE* q;

  err = cd_obj_init_syms(obj);
  if (!cd_is_ok(err))
    return err;

  assert(sizeof(void*) == sizeof(*addr));
  res = cd_hashmap_get(&obj->syms, sym, strlen(sym));
  if (res == NULL)
    goto not_found;

  *addr = (uint64_t) res;
  return cd_ok();

not_found:
  /* Lookup sym in dsos */
  QUEUE_FOREACH(q, &obj->dso) {
    cd_obj_t* dso;

    dso = container_of(q, cd_obj_t, member);
    err = cd_obj_get_sym(dso, sym, addr);
    if (cd_is_ok(err) || err.code != kCDErrNotFound)
      return err;
  }

  return cd_error_str(kCDErrNotFound, sym);
}


cd_error_t cd_obj_count_segs(cd_obj_t* obj,
                             cd_segment_t* seg,
                             void* arg) {
  int* counter;

  counter = arg;
  *counter = *counter + 1;
  return cd_ok();
}


cd_error_t cd_obj_fill_segs(cd_obj_t* obj,
                            cd_segment_t* seg,
                            void* arg) {
  cd_segment_t** ptr;

  ptr = arg;

  /* Copy the segment */
  **ptr = *seg;

  /* Fill the splay tree */
  cd_splay_insert(&obj->seg_splay, *ptr);

  /* Move the pointer forward */
  *ptr = *ptr + 1;

  return cd_ok();
}


cd_error_t cd_obj_init_segments(cd_obj_t* obj) {
  cd_error_t err;
  cd_segment_t* seg;

  /* Already initialized */
  if (obj->segment_count != -1)
    return cd_ok();

  cd_splay_init(&obj->seg_splay,
                (int (*)(const void*, const void*)) cd_segment_sort);
  obj->segment_count = 0;

  err = cd_obj_iterate_segs(obj, cd_obj_count_segs, &obj->segment_count);
  if (!cd_is_ok(err))
    return err;

  obj->segments = calloc(obj->segment_count, sizeof(*obj->segments));
  if (obj->segments == NULL)
    return cd_error_str(kCDErrNoMem, "cd_segment_t");

  seg = obj->segments;
  return cd_obj_iterate_segs(obj, cd_obj_fill_segs, &seg);
}


cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res) {
  cd_error_t err;
  cd_segment_t idx;
  cd_segment_t* r;

  if (!cd_obj_is_core(obj))
    return cd_error(kCDErrNotCore);

  err = cd_obj_init_segments(obj);
  if (!cd_is_ok(err))
    return err;

  if (obj->segment_count == 0)
    return cd_error(kCDErrNotFound);

  idx.start = addr;
  r = cd_splay_find(&obj->seg_splay, &idx);
  if (r == NULL)
    return cd_error(kCDErrNotFound);

  if (addr + size > r->end)
    return cd_error(kCDErrNotFound);

  *res = r->ptr + (addr - r->start);

  return cd_ok();
}


int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b) {
  return a->start > b->start ? 1 : a->start == b->start ? 0 : -1;
}


int cd_symbol_sort(const cd_sym_t* a, const cd_sym_t* b) {
  return a->value > b->value ? 1 : a->value == b->value ? 0 : -1;
}


cd_error_t cd_obj_internal_init(cd_obj_t* obj) {
  QUEUE_INIT(&obj->member);

  /* Dynamic libraries */
  QUEUE_INIT(&obj->dso);

  obj->has_syms = 0;
  obj->segment_count = -1;
  obj->segments = NULL;
  obj->aslr = 0;
  obj->cfa = NULL;

  return cd_ok();
}


void cd_obj_internal_free(cd_obj_t* obj) {
  if (obj->has_syms) {
    cd_splay_destroy(&obj->sym_splay);
    cd_hashmap_destroy(&obj->syms);
  }
  obj->has_syms = 0;

  if (obj->segment_count != -1) {
    free(obj->segments);
    cd_splay_destroy(&obj->seg_splay);
  }

  /* Free DSOs */
  while (!QUEUE_EMPTY(&obj->dso)) {
    QUEUE* q;
    cd_obj_t* dso;

    q = QUEUE_HEAD(&obj->dso);
    dso = container_of(q, cd_obj_t, member);

    dso->method->obj_free(dso);
  }
  if (obj->cfa != NULL) {
    cd_dwarf_free_cfa(obj->cfa);
    obj->cfa = NULL;
  }

  close(obj->fd);
  obj->fd = -1;

  if (!QUEUE_EMPTY(&obj->member))
    QUEUE_REMOVE(&obj->member);
}


cd_error_t cd_obj_lookup_ip(cd_obj_t* obj,
                            uint64_t addr,
                            cd_sym_t** res,
                            cd_dwarf_fde_t** fde) {
  cd_error_t err;
  cd_sym_t idx;
  QUEUE* q;

  err = cd_obj_init_syms(obj);
  if (!cd_is_ok(err))
    return err;

  idx.value = addr;
  *res = cd_splay_find(&obj->sym_splay, &idx);
  if (*res == NULL)
    goto not_found;

  if ((*res)->name == NULL && (*res)->nlen == 0)
    goto not_found;

  /* Get FDE */
  err = cd_obj_init_dwarf(obj);
  if (!cd_is_ok(err))
    return err;

  if (obj->cfa == NULL)
    goto not_found;

  *fde = NULL;
  err = cd_dwarf_get_fde(obj->cfa, addr - obj->aslr, fde);
  if (err.code == kCDErrNotFound)
    *fde = NULL;
  else if (!cd_is_ok(err))
    return err;
  /* Check that FDE covers the symbol */
  else if ((*fde)->init_loc + obj->aslr + (*fde)->range < addr) {
    *fde = NULL;
    return cd_ok();
  }

  return cd_ok();

not_found:
  /* Lookup ip in dsos */
  QUEUE_FOREACH(q, &obj->dso) {
    cd_obj_t* dso;

    dso = container_of(q, cd_obj_t, member);
    err = cd_obj_lookup_ip(dso, addr, res, fde);
    if (cd_is_ok(err) || err.code != kCDErrNotFound)
      return err;
  }

  return cd_error(kCDErrNotFound);
}


cd_error_t cd_obj_iterate_stack(cd_obj_t* obj,
                                int thread_id,
                                cd_iterate_stack_cb cb,
                                void* arg) {
  cd_error_t err;
  cd_obj_thread_t last;
  cd_obj_thread_t cur;
  char* stack;
  uint64_t start;
  uint64_t stack_size;

  err = cd_obj_get_thread(obj, thread_id, &cur);
  if (!cd_is_ok(err))
    return err;

  stack_size = cur.stack.bottom - cur.stack.top;
  start = cur.stack.top;
  err = cd_obj_get(obj, start,stack_size, (void**) &stack);
  if (!cd_is_ok(err))
    return err;

  while (cur.stack.top >= start &&
         cur.stack.top < start + stack_size &&
         cur.stack.frame > 0) {
    cd_sym_t* sym;
    cd_dwarf_fde_t* fde;
    cd_frame_t frame;

    last = cur;

    err = cd_obj_lookup_ip(obj, last.regs.ip, &sym, &fde);
    if (err.code == kCDErrNotFound) {
      fde = NULL;
      sym = NULL;
    } else if (!cd_is_ok(err)) {
      return err;
    }

    frame.ip = last.regs.ip;
    if (sym == NULL) {
      frame.sym = NULL;
      frame.sym_len = 0;
    } else {
      frame.sym = sym->name;
      frame.sym_len = sym->nlen;
    }

    /* No FDE case, just use defaults */
    if (fde == NULL) {
      uint64_t off;

      off = last.stack.frame - start;

      /* Next frame */
      cur.regs.ip =
          *(uint64_t*) (stack + off + (cd_obj_is_x64(obj) ? 8 : 4));
      cur.stack.frame = *(uint64_t*) (stack + off);

      /* Change thread state, so the FDE emulator could see it */
      cur.stack.top = last.stack.frame + (cd_obj_is_x64(obj) ? 16 : 8);
    } else {
      /* Use FDE to figure out thread state before entering the proc */
      err = cd_dwarf_fde_run(fde,
                             stack,
                             stack_size,
                             start,
                             &last,
                             &cur);
      if (!cd_is_ok(err))
        return err;
    }

    frame.start = stack + (cur.stack.top - start);
    frame.stop = stack + (last.stack.top - start);
    frame.frame = stack + (last.stack.frame - start);

    /* End of stack */
    if (cur.stack.top >= start + stack_size)
      break;

    err = cb(obj, &frame, arg);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_error(kCDErrOk);
}


cd_error_t cd_obj_add_dso(cd_obj_t* obj, cd_obj_t* dso) {
  QUEUE_INSERT_TAIL(&obj->dso, &dso->member);
  return cd_ok();
}


cd_error_t cd_obj_add_binary(cd_obj_t* obj, cd_obj_t* dso) {
  /*
   * No other DSOs found when loading the core, use link info from the
   * binary
   */
  if (cd_obj_is_core(obj) &&
      QUEUE_EMPTY(&obj->member) &&
      obj->method->obj_use_binary != NULL) {
    cd_error_t err;

    err = obj->method->obj_use_binary(obj, dso);
    if (!cd_is_ok(err))
      return err;
  }

  QUEUE_INSERT_HEAD(&obj->dso, &dso->member);
  return cd_ok();
}


cd_error_t cd_obj_init_aslr(cd_obj_t* obj, cd_obj_opts_t* opts) {
  cd_error_t err;
  int i;

  /* Figure out the ASLR slide value */
  err = cd_obj_init_segments((cd_obj_t*) obj);
  if (!cd_is_ok(err))
    return err;

  for (i = 0; i < obj->segment_count; i++) {
    cd_segment_t* seg;

    seg = &obj->segments[i];
    if (seg->fileoff != 0 || seg->sects == 0)
      continue;

    obj->aslr = (int64_t) opts->reloc - seg->start;
    break;
  }

  if (i == obj->segment_count)
    return cd_error_str(kCDErrNotFound, "0-fileoff dyld segment");

  return cd_ok();
}
