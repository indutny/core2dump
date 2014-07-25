#include "obj-common.h"
#include "common.h"
#include "error.h"
#include "obj.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>


typedef struct cd_common_obj_s cd_common_obj_t;

struct cd_common_obj_s {
  CD_OBJ_COMMON_FIELDS
};

static const int kCDSymtabInitialSize = 16384;

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


int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b) {
  return a->start > b->start ? 1 : a->start == b->start ? 0 : -1;
}
