#include "common.h"
#include "error.h"
#include "obj.h"
#include "obj-internal.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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


/* Wrappers around method */

cd_obj_t* cd_obj_new(cd_obj_method_t* method, int fd, cd_error_t* err) {
  return cd_obj_new_ex(method, fd, NULL, err);
}


cd_obj_t* cd_obj_new_ex(cd_obj_method_t* method,
                        int fd,
                        void* opts,
                        cd_error_t* err) {
  cd_obj_t* res;

  res = method->obj_new(fd, opts, err);
  if (cd_is_ok(*err))
    res->method = method;

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


cd_error_t cd_obj_iterate_segs(cd_obj_t* obj,
                               cd_obj_iterate_seg_cb cb,
                               void* arg) {
  return obj->method->obj_iterate_segs(obj, cb, arg);
}


/* Just a common implementation */


cd_error_t cd_obj_insert_syms(cd_obj_t* obj, cd_sym_t* sym, void* arg) {
  cd_sym_t* copy;

  /* Skip empty symbols */
  if (sym->nlen == 0 || sym->value == 0)
    return cd_ok();

  if (cd_hashmap_insert(&obj->syms,
                        sym->name,
                        sym->nlen,
                        (void*) sym->value) != 0) {
    return cd_error_str(kCDErrNoMem, "cd_hashmap_insert");
  }

  copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return cd_error_str(kCDErrNoMem, "cd_sym_t");
  *copy = *sym;

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
  copy->value = seg->end;

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

  if (cd_hashmap_init(&obj->syms, kCDSymtabInitialSize, 0) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_t");
  obj->has_syms = 1;

  err = cd_obj_iterate_syms((cd_obj_t*) obj, cd_obj_insert_syms, NULL);
  if (!cd_is_ok(err))
    return err;

  return cd_obj_iterate_segs((cd_obj_t*) obj,
                             cd_obj_insert_seg_ends,
                             NULL);
}


cd_error_t cd_obj_get_sym(cd_obj_t* obj,
                          const char* sym,
                          uint64_t* addr) {
  cd_error_t err;
  void* res;

  err = cd_obj_init_syms(obj);
  if (!cd_is_ok(err))
    return err;

  assert(sizeof(void*) == sizeof(*addr));
  res = cd_hashmap_get(&obj->syms, sym, strlen(sym));
  if (res == NULL)
    return cd_error_str(kCDErrNotFound, sym);

  *addr = (uint64_t) res;
  return cd_ok();
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
  if (cd_splay_insert(&obj->seg_splay, *ptr) != 0)
    return cd_error_str(kCDErrNoMem, "seg_splay");

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

  return cd_ok();
}


void cd_obj_internal_free(cd_obj_t* obj) {
  if (obj->has_syms)
    cd_hashmap_destroy(&obj->syms);
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
    QUEUE_REMOVE(q);
    dso = container_of(q, cd_obj_t, member);

    dso->method->obj_free(dso);
  }
}


cd_error_t cd_obj_lookup_ip(cd_obj_t* obj,
                            uint64_t addr,
                            const char** sym,
                            int* sym_len) {
  cd_error_t err;
  cd_sym_t idx;
  cd_sym_t* r;

  err = cd_obj_init_syms(obj);
  if (!cd_is_ok(err))
    return err;

  idx.value = addr;
  r = cd_splay_find(&obj->sym_splay, &idx);
  if (r == NULL)
    return cd_error(kCDErrNotFound);

  if (r->name == NULL && r->nlen == 0)
    return cd_error(kCDErrNotFound);

  *sym = r->name;
  *sym_len = r->nlen;


  return cd_ok();
}


cd_error_t cd_obj_iterate_stack(cd_obj_t* obj,
                                int thread_id,
                                cd_iterate_stack_cb cb,
                                void* arg) {
  cd_error_t err;
  cd_obj_thread_t thread;
  char* stack;
  uint64_t stack_size;
  uint64_t frame_start;
  uint64_t frame_end;
  uint64_t ip;

  err = cd_obj_get_thread(obj, thread_id, &thread);
  if (!cd_is_ok(err))
    return err;

  stack_size = thread.stack.bottom - thread.stack.top;
  err = cd_obj_get(obj,
                   thread.stack.top,
                   stack_size,
                   (void**) &stack);
  if (!cd_is_ok(err))
    return err;

  if (thread.stack.frame <= thread.stack.top)
    return cd_error(kCDErrStackOOB);

  frame_start = thread.stack.frame - thread.stack.top;
  frame_end = 0;
  ip = thread.regs.ip;
  while (frame_start < stack_size) {
    cd_frame_t frame;

    frame.start = stack + frame_start;
    frame.stop = stack + frame_end;
    frame.ip = ip;
    err = cb(obj, &frame, arg);
    if (!cd_is_ok(err))
      return err;

    /* Next frame */
    ip = *(uint64_t*) (stack + frame_start + (cd_obj_is_x64(obj) ? 8 : 4));
    frame_end = frame_start;
    frame_start = *(uint64_t*) (stack + frame_start);
    if (frame_start <= thread.stack.top)
      break;

    frame_start -= thread.stack.top;
    if (frame_start >= stack_size)
      break;
  }

  return cd_error(kCDErrOk);
}
