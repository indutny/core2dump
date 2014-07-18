#ifndef SRC_V8_HELPERS_H_
#define SRC_V8_HELPERS_H_

#include "state.h"
#include "error.h"

/* Check object tag */

#define V8_IS_HEAPOBJECT(ptr)                                                 \
    ((((intptr_t) ptr) & cd_v8_HeapObjectTagMask) == cd_v8_HeapObjectTag)

/* Untag object */

#define V8_OBJ(ptr) ((void*) ((char*) ptr - cd_v8_HeapObjectTag))

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

cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* map,
                              int type,
                              int* size);

#endif  /* SRC_V8_HELPERS_H_ */
