#include "v8helpers.h"
#include "error.h"
#include "state.h"
#include "v8constants.h"

#include <stdio.h>
#include <string.h>

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


cd_error_t cd_v8_to_cstr(cd_state_t* state, void* str, const char** res) {
  void** ptr;
  void* map;
  int type;
  int encoding;
  int repr;
  int length;
  char* data;

  /* Determine string's type */
  V8_CORE_PTR(str, cd_v8_class_HeapObject__map__Map, ptr);

  /* Ignore zap bit */
  map = (void*) ((intptr_t) *ptr & (~state->zap_bit));

  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, ptr);
  type = *(uint8_t*) ptr;

  if (type > cd_v8_FirstNonstringType)
    return cd_error(kCDErrNotString);

  /* kOneByteStringTag or kTwoByteStringTag */
  encoding = type & cd_v8_StringEncodingMask;
  /* kSeqStringTag, kExternalStringTag, kSlicedStringTag, kConsStringTag */
  repr = type & cd_v8_StringRepresentationMask;

  if (encoding == cd_v8_AsciiStringTag && repr == cd_v8_SeqStringTag) {
    V8_CORE_PTR(str, cd_v8_class_String__length__SMI, ptr);
    length = V8_SMI(*ptr);

    V8_CORE_PTR(str, cd_v8_class_SeqOneByteString__chars__char, ptr);
    data = (char*) ptr;
  } else {
    data = NULL;
    length = 0;
  }

  if (data == NULL)
    return cd_error(kCDErrNotFound);

  return cd_strings_copy(&state->strings, res, data, length);
}
