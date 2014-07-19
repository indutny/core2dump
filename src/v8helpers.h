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
    do {                                                                      \
      cd_error_t err;                                                         \
      err = cd_obj_get(state->core,                                           \
                       (uint64_t) ((char*) V8_OBJ(ptr) + (off)),              \
                       sizeof(*(out)),                                        \
                       (void**) &(out));                                      \
      if (!cd_is_ok(err))                                                     \
        return err;                                                           \
    } while (0);                                                              \

/* Untag SMI */
#define V8_SMI(ptr)                                                           \
    ((uint32_t) ((intptr_t) (ptr) >> (cd_v8_SmiShiftSize +                    \
                                      cd_v8_SmiTagMask)))                     \

cd_error_t cd_v8_get_obj_type(cd_state_t* state,
                              void* obj,
                              void* map,
                              int* type);
cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* map,
                              int type,
                              int* size);
cd_error_t cd_v8_to_cstr(cd_state_t* state,
                         void* str,
                         const char** res,
                         int* index);
cd_error_t cd_v8_fn_name(cd_state_t* state,
                         void* fn,
                         const char** res,
                         int* index);

#endif  /* SRC_V8_HELPERS_H_ */
