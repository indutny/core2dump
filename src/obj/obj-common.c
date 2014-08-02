#include "obj-common.h"
#include "common.h"
#include "error.h"
#include "obj.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


typedef struct cd_common_obj_s cd_common_obj_t;

struct cd_common_obj_s {
  CD_OBJ_COMMON_FIELDS
};

static const int kCDSymtabInitialSize = 16384;

static cd_error_t cd_obj_count_segs(struct cd_obj_s* obj,
                                    cd_segment_t* seg,
                                    void* arg);
static cd_error_t cd_obj_fill_segs(struct cd_obj_s* obj,
                                   cd_segment_t* seg,
                                   void* arg);
static cd_error_t cd_obj_insert_seg_ends(struct cd_obj_s* obj,
                                         cd_segment_t* seg,
                                         void* arg);
static int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b);
static int cd_symbol_sort(const cd_sym_t* a, const cd_sym_t* b);
static cd_error_t cd_obj_init_syms(cd_common_obj_t* obj);
static cd_error_t cd_obj_insert_syms(struct cd_obj_s* obj,
                                     cd_sym_t* sym,
                                     void* arg);


cd_error_t cd_obj_insert_syms(struct cd_obj_s* obj, cd_sym_t* sym, void* arg) {
  cd_common_obj_t* cobj;
  cd_sym_t* copy;

  /* Skip empty symbols */
  if (sym->nlen == 0 || sym->value == 0)
    return cd_ok();

  cobj = (cd_common_obj_t*) obj;
  if (cd_hashmap_insert(&cobj->syms,
                        sym->name,
                        sym->nlen,
                        (void*) sym->value) != 0) {
    return cd_error_str(kCDErrNoMem, "cd_hashmap_insert");
  }

  copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return cd_error_str(kCDErrNoMem, "cd_sym_t");
  *copy = *sym;

  if (cd_splay_insert(&cobj->sym_splay, copy) != 0)
    free(copy);

  return cd_ok();
}


cd_error_t cd_obj_insert_seg_ends(struct cd_obj_s* obj,
                                  cd_segment_t* seg,
                                  void* arg) {
  cd_common_obj_t* cobj;
  cd_sym_t* copy;

  cobj = (cd_common_obj_t*) obj;

  copy = malloc(sizeof(*copy));
  if (copy == NULL)
    return cd_error_str(kCDErrNoMem, "cd_sym_t");
  copy->name = NULL;
  copy->nlen = 0;
  copy->value = seg->end;

  if (cd_splay_insert(&cobj->sym_splay, copy) != 0)
    free(copy);

  return cd_ok();
}


cd_error_t cd_obj_init_syms(cd_common_obj_t* obj) {
  cd_error_t err;

  if (obj->has_syms)
    return cd_ok();

  cd_splay_init(&obj->sym_splay,
                (int (*)(const void*, const void*)) cd_symbol_sort);
  obj->sym_splay.allocated = 1;

  if (cd_hashmap_init(&obj->syms, kCDSymtabInitialSize, 0) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_t");
  obj->has_syms = 1;

  err = cd_obj_iterate_syms((struct cd_obj_s*) obj, cd_obj_insert_syms, NULL);
  if (!cd_is_ok(err))
    return err;

  return cd_obj_iterate_segs((struct cd_obj_s*) obj,
                             cd_obj_insert_seg_ends,
                             NULL);
}


cd_error_t cd_obj_get_sym(struct cd_obj_s* obj,
                          const char* sym,
                          uint64_t* addr) {
  cd_error_t err;
  void* res;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  err = cd_obj_init_syms(cobj);
  if (!cd_is_ok(err))
    return err;

  assert(sizeof(void*) == sizeof(*addr));
  res = cd_hashmap_get(&cobj->syms, sym, strlen(sym));
  if (res == NULL)
    return cd_error_str(kCDErrNotFound, sym);

  *addr = (uint64_t) res;
  return cd_ok();
}


cd_error_t cd_obj_count_segs(struct cd_obj_s* obj,
                             cd_segment_t* seg,
                             void* arg) {
  int* counter;

  counter = arg;
  *counter = *counter + 1;
  return cd_ok();
}


cd_error_t cd_obj_fill_segs(struct cd_obj_s* obj,
                            cd_segment_t* seg,
                            void* arg) {
  cd_segment_t** ptr;

  ptr = arg;

  /* Copy the segment */
  **ptr = *seg;

  /* Fill the splay tree */
  if (cd_splay_insert(&((cd_common_obj_t*) obj)->seg_splay, *ptr) != 0)
    return cd_error_str(kCDErrNoMem, "seg_splay");

  /* Move the pointer forward */
  *ptr = *ptr + 1;

  return cd_ok();
}


cd_error_t cd_obj_init_segments(cd_obj_t* obj) {
  cd_error_t err;
  cd_segment_t* seg;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  /* Already initialized */
  if (cobj->segment_count != -1)
    return cd_ok();

  cd_splay_init(&cobj->seg_splay,
                (int (*)(const void*, const void*)) cd_segment_sort);
  cobj->segment_count = 0;

  err = cd_obj_iterate_segs(obj, cd_obj_count_segs, &cobj->segment_count);
  if (!cd_is_ok(err))
    return err;

  cobj->segments = calloc(cobj->segment_count, sizeof(*cobj->segments));
  if (cobj->segments == NULL)
    return cd_error_str(kCDErrNoMem, "cd_segment_t");

  seg = cobj->segments;
  return cd_obj_iterate_segs(obj, cd_obj_fill_segs, &seg);
}


cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res) {
  cd_error_t err;
  cd_segment_t idx;
  cd_segment_t* r;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  if (!cd_obj_is_core(obj))
    return cd_error(kCDErrNotCore);

  err = cd_obj_init_segments(obj);
  if (!cd_is_ok(err))
    return err;

  if (cobj->segment_count == 0)
    return cd_error(kCDErrNotFound);

  idx.start = addr;
  r = cd_splay_find(&cobj->seg_splay, &idx);
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


cd_error_t cd_obj_common_init(struct cd_obj_s* obj) {
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;
  cobj->has_syms = 0;
  cobj->segment_count = -1;
  cobj->segments = NULL;

  return cd_ok();
}


void cd_obj_common_free(struct cd_obj_s* obj) {
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  if (cobj->has_syms)
    cd_hashmap_destroy(&cobj->syms);
  cobj->has_syms = 0;

  if (cobj->segment_count != -1) {
    free(cobj->segments);
    cd_splay_destroy(&cobj->seg_splay);
  }
}


int cd_obj_is_x64(struct cd_obj_s* obj) {
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;
  return cobj->is_x64;
}


cd_error_t cd_obj_lookup_ip(cd_obj_t* obj,
                            uint64_t addr,
                            const char** sym,
                            int* sym_len) {
  cd_error_t err;
  cd_sym_t idx;
  cd_sym_t* r;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;
  err = cd_obj_init_syms(cobj);
  if (!cd_is_ok(err))
    return err;

  idx.value = addr;
  r = cd_splay_find(&cobj->sym_splay, &idx);
  if (r == NULL)
    return cd_error(kCDErrNotFound);

  if (r->name == NULL && r->nlen == 0)
    return cd_error(kCDErrNotFound);

  *sym = r->name;
  *sym_len = r->nlen;


  return cd_ok();
}
