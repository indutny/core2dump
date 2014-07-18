#include "v8helpers.h"
#include "error.h"
#include "state.h"
#include "v8constants.h"


cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* map,
                              int type,
                              int* size) {
  int instance_size;
  uint8_t* ptr;

  V8_CORE_PTR(map, cd_v8_class_Map__instance_size__int, ptr);
  instance_size = *ptr;

  /* Constant size */
  if (instance_size != 0) {
    *size = instance_size * (cd_obj_is_x64(state->core) ? 8 : 4);
    return cd_ok();
  }

  /* Variable-size */

  *size = 0;
  return cd_ok();
}
