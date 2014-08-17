#include <stdint.h>

#include "v8constants.h"
#include "error.h"
#include "obj.h"

#define CD_V8_CONSTANT_VALUE(V) int cd_v8_##V;
CD_V8_REQUIRED_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
#undef CD_V8_CONSTANT_VALUE
#define CD_V8_CONSTANT_VALUE(V, D) int cd_v8_##V;
CD_V8_OPTIONAL_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
#undef CD_V8_CONSTANT_VALUE

static int cd_v8_initialized;

#define CD_V8_LOAD_REQUIRED_CONSTANT(V)                                       \
    do {                                                                      \
      cd_error_t err;                                                         \
      uint64_t addr;                                                          \
      void* location;                                                         \
      err = cd_obj_get_sym(core, "_v8dbg_" #V, &addr);                        \
      if (!cd_is_ok(err))                                                     \
        err = cd_obj_get_sym(core, "v8dbg_" #V, &addr);                       \
      if (!cd_is_ok(err))                                                     \
        return err;                                                           \
      err = cd_obj_get(core, addr, sizeof(int), &location);                   \
      if (!cd_is_ok(err))                                                     \
        return err;                                                           \
      cd_v8_##V = *(int*) location;                                           \
    } while (0);                                                              \


#define CD_V8_LOAD_OPTIONAL_CONSTANT(V, D)                                    \
    do {                                                                      \
      cd_error_t err;                                                         \
      uint64_t addr;                                                          \
      void* location;                                                         \
      err = cd_obj_get_sym(core, "_v8dbg_" #V, &addr);                        \
      if (!cd_is_ok(err))                                                     \
        err = cd_obj_get_sym(core, "v8dbg_" #V, &addr);                       \
      if (!cd_is_ok(err)) {                                                   \
        cd_v8_##V = (D);                                                      \
        break;                                                                \
      }                                                                       \
      err = cd_obj_get(core, addr, sizeof(int), &location);                   \
      if (!cd_is_ok(err)) {                                                   \
        cd_v8_##V = (D);                                                      \
        break;                                                                \
      }                                                                       \
      cd_v8_##V = *(int*) location;                                           \
      break;                                                                  \
    } while (0);                                                              \

cd_error_t cd_v8_init(cd_obj_t* core) {
  if (cd_v8_initialized)
    return cd_ok();

  CD_V8_REQUIRED_CONSTANTS_ENUM(CD_V8_LOAD_REQUIRED_CONSTANT);
  CD_V8_OPTIONAL_CONSTANTS_ENUM(CD_V8_LOAD_OPTIONAL_CONSTANT);

  cd_v8_initialized = 1;
  return cd_ok();
}

#undef CD_V8_LOAD_REQUIRED_CONSTANT
#undef CD_V8_LOAD_OPTIONAL_CONSTANT
