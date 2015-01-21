#include <stdint.h>
#include <stdio.h>

#include "v8constants.h"
#include "error.h"
#include "obj.h"

#define CD_V8_CONSTANT_VALUE(V, D) int cd_v8_##V;
CD_V8_REQUIRED_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
CD_V8_OPTIONAL_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
#undef CD_V8_CONSTANT_VALUE

static int cd_v8_initialized;

#define CD_V8_LOAD_CONSTANT(V, D, VERBOSE)                                    \
    do {                                                                      \
      cd_error_t err;                                                         \
      uint64_t addr;                                                          \
      void* location;                                                         \
      err = cd_obj_get_sym(core, "_v8dbg_" #V, &addr);                        \
      if (!cd_is_ok(err))                                                     \
        err = cd_obj_get_sym(core, "v8dbg_" #V, &addr);                       \
      if (cd_is_ok(err))                                                      \
        err = cd_obj_get(core, addr, sizeof(int), &location);                 \
      if (!cd_is_ok(err)) {                                                   \
        cd_v8_##V = (D);                                                      \
        if ((VERBOSE))                                                        \
          fprintf(stderr, "Constant: " #V " was not found\n");                \
        break;                                                                \
      }                                                                       \
      cd_v8_##V = *(int*) location;                                           \
      break;                                                                  \
    } while (0);                                                              \

#define CD_V8_LOAD_REQUIRED_CONSTANT(V, D)                                    \
    CD_V8_LOAD_CONSTANT(V, D, 1)


#define CD_V8_LOAD_OPTIONAL_CONSTANT(V, D)                                    \
    CD_V8_LOAD_CONSTANT(V, D, 0)

cd_error_t cd_v8_init(cd_obj_t* core) {
  int ptr_size;

  if (cd_v8_initialized)
    return cd_ok();

  /* Used in some optional consts */
  ptr_size = core->is_x64 ? 8 : 4;
  CD_V8_REQUIRED_CONSTANTS_ENUM(CD_V8_LOAD_REQUIRED_CONSTANT);
  CD_V8_OPTIONAL_CONSTANTS_ENUM(CD_V8_LOAD_OPTIONAL_CONSTANT);

  cd_v8_initialized = 1;
  return cd_ok();
}

#undef CD_V8_LOAD_REQUIRED_CONSTANT
#undef CD_V8_LOAD_OPTIONAL_CONSTANT
