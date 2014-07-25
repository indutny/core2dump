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
static cd_error_t cd_obj_init_segments(cd_common_obj_t* cobj);
static int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b);
static cd_error_t cd_obj_init_syms(struct cd_obj_s* obj,
                                   const char* name,
                                   int nlen,
                                   uint64_t value,
                                   void* arg);


cd_error_t cd_obj_init_syms(struct cd_obj_s* obj,
                            const char* name,
                            int nlen,
                            uint64_t value,
                            void* arg) {
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;
  if (cd_hashmap_insert(&cobj->syms, name, nlen, (void*) value) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_insert");

  return cd_ok();
}


cd_error_t cd_obj_get_sym(struct cd_obj_s* obj,
                          const char* sym,
                          uint64_t* addr) {
  cd_error_t err;
  void* res;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  if (!cobj->has_syms) {
    if (cd_hashmap_init(&cobj->syms, kCDSymtabInitialSize, 0) != 0)
      return cd_error_str(kCDErrNoMem, "cd_hashmap_t");
    cobj->has_syms = 1;

    err = cd_obj_iterate_syms(obj, cd_obj_init_syms, NULL);
    if (!cd_is_ok(err))
      return err;
  }

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


cd_error_t cd_obj_init_segments(cd_common_obj_t* obj) {
  cd_error_t err;
  cd_segment_t* seg;

  /* Already initialized */
  if (obj->segment_count != -1)
    return cd_ok();

  cd_splay_init(&obj->seg_splay,
                (int (*)(const void*, const void*)) cd_segment_sort);
  obj->segment_count = 0;

  err = cd_obj_iterate_segs((struct cd_obj_s*) obj,
                            cd_obj_count_segs,
                            &obj->segment_count);
  if (!cd_is_ok(err))
    return err;

  obj->segments = calloc(obj->segment_count, sizeof(*obj->segments));
  if (obj->segments == NULL)
    return cd_error_str(kCDErrNoMem, "cd_segment_t");

  seg = obj->segments;
  return cd_obj_iterate_segs((struct cd_obj_s*) obj, cd_obj_fill_segs, &seg);
}


cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res) {
  cd_error_t err;
  cd_segment_t idx;
  cd_segment_t* r;
  cd_common_obj_t* cobj;

  cobj = (cd_common_obj_t*) obj;

  if (!cd_obj_is_core(obj))
    return cd_error(kCDErrNotCore);

  err = cd_obj_init_segments(cobj);
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
