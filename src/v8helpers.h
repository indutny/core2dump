#ifndef SRC_V8_HELPERS_H_
#define SRC_V8_HELPERS_H_

#include "state.h"
#include "error.h"

/* Check object tag */

#define V8_IS_HEAPOBJECT(ptr)                                                 \
    ((((intptr_t) (ptr)) & cd_v8_HeapObjectTagMask) == cd_v8_HeapObjectTag)

/* Untag object */

#define V8_OBJ(ptr) ((void*) ((char*) (ptr) - cd_v8_HeapObjectTag))

/* Pointer lookup in a core file */

#define V8_CORE_PTR(ptr, off, out)                                            \
    V8_CORE_DATA(ptr, off, out, state->ptr_size)                              \


#define V8_CORE_DATA(ptr, off, out, size)                                     \
    do {                                                                      \
      cd_error_t err;                                                         \
      err = cd_obj_get(state->core,                                           \
                       (uint64_t) ((char*) V8_OBJ(ptr) + (off)),              \
                       (size),                                                \
                       (void**) &(out));                                      \
      if (!cd_is_ok(err))                                                     \
        return err;                                                           \
    } while (0);                                                              \

/* Check SMI */
#define V8_IS_SMI(ptr) (((intptr_t) (ptr) & cd_v8_SmiTagMask) == cd_v8_SmiTag)

/* Untag SMI */
#define V8_SMI(ptr)                                                           \
    ((int32_t) ((intptr_t) (ptr) >> (cd_v8_SmiShiftSize +                     \
                                     cd_v8_SmiTagMask)))                      \

/* Tag SMI */
#define V8_TAG_SMI(num)                                                       \
    ((void*) (((intptr_t) (num) << (cd_v8_SmiShiftSize + cd_v8_SmiTagMask)) | \
        cd_v8_SmiTag))                                                        \

typedef struct cd_script_s cd_script_t;

struct cd_script_s {
  void* ptr;
  int id;

  const char* name;
  int name_len;
  int name_idx;

  int line;
  int column;
};

cd_error_t cd_v8_get_obj_type(cd_state_t* state,
                              void* obj,
                              void* map,
                              int* type);
cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* obj,
                              void* map,
                              int type,
                              int* size);
cd_error_t cd_v8_to_cstr(cd_state_t* state,
                         void* str,
                         const char** res,
                         int* len,
                         int* index);
cd_error_t cd_v8_fn_info(cd_state_t* state,
                         void* fn,
                         const char** res,
                         int* len,
                         int* index,
                         cd_script_t* script);
cd_error_t cd_v8_script_info(cd_state_t* state, void* obj, cd_script_t* res);
cd_error_t cd_v8_obj_has_fast_props(cd_state_t* state,
                                    void* obj,
                                    void* map,
                                    int* fast);
cd_error_t cd_v8_obj_has_fast_elems(cd_state_t* state,
                                    void* obj,
                                    void* map,
                                    int* fast);
cd_error_t cd_v8_get_fixed_arr_len(cd_state_t* state, void* arr, int* size);
cd_error_t cd_v8_get_fixed_arr_data(cd_state_t* state,
                                    void* arr,
                                    void** data,
                                    int* size);
cd_error_t cd_v8_is_hole(cd_state_t* state, void* value, int* is_hole);

#endif  /* SRC_V8_HELPERS_H_ */
