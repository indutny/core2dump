#include "v8helpers.h"
#include "error.h"
#include "state.h"
#include "v8constants.h"

#include <stdio.h>
#include <string.h>


#define LAZY_MAP                                                              \
    if (map == NULL) {                                                        \
      void** pmap;                                                            \
      V8_CORE_PTR(obj, cd_v8_class_HeapObject__map__Map, pmap);               \
      map = *pmap;                                                            \
    }                                                                         \
    if (!V8_IS_HEAPOBJECT(map))                                               \
      return cd_error(kCDErrNotObject);                                       \


cd_error_t cd_v8_get_obj_type(cd_state_t* state,
                              void* obj,
                              void* map,
                              int* type) {
  uint8_t* ptype;

  LAZY_MAP

  /* Load object type */
  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, ptype);
  *type = (int) *ptype;

  return cd_ok();
}


cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* obj,
                              void* map,
                              int type,
                              int* size) {
  int instance_size;
  uint8_t* ptr;

  LAZY_MAP

  V8_CORE_PTR(map, cd_v8_class_Map__instance_size__int, ptr);
  instance_size = *ptr;

  /* Constant size */
  if (instance_size != 0) {
    *size = instance_size * state->ptr_size;
    return cd_ok();
  }

  /* Variable-size */
  if (type == CD_V8_TYPE(FixedArray, FIXED_ARRAY) ||
      type == CD_V8_TYPE(FixedDoubleArray, FIXED_DOUBLE_ARRAY)) {
    cd_error_t err;

    err = cd_v8_get_fixed_arr_len(state, obj, size);
    if (!cd_is_ok(err))
      return err;

    *size *= 8;

    /* We are returning object size, not array size */
    *size += cd_v8_class_FixedArray__data__uintptr_t;
    return cd_ok();
  }
  /* TODO(indutny) Support Code, and others */

  *size = 0;
  return cd_ok();
}


cd_error_t cd_v8_to_cstr(cd_state_t* state,
                         void* str,
                         const char** res,
                         int* index) {
  void** ptr;
  int type;
  int encoding;
  int repr;
  int length;
  char* data;
  cd_error_t err;

  /* Determine string's type */
  err = cd_v8_get_obj_type(state, str, NULL, &type);
  if (!cd_is_ok(err))
    return err;

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

  return cd_strings_copy(&state->strings, res, index, data, length);
}


cd_error_t cd_v8_fn_name(cd_state_t* state,
                         void* fn,
                         const char** res,
                         int* index) {
  void** ptr;
  void* sh;
  void* name;

  /* Load shared function info to lookup name */
  V8_CORE_PTR(fn, cd_v8_class_JSFunction__shared__SharedFunctionInfo, ptr);
  sh = *ptr;

  V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
  name = *ptr;

  return cd_v8_to_cstr(state, name, res, index);
}


cd_error_t cd_v8_obj_has_fast_props(cd_state_t* state,
                                    void* obj,
                                    void* map,
                                    int* fast) {
  void** ptr;
  int bit3;

  V8_CORE_PTR(map, cd_v8_class_Map__bit_field3__SMI, ptr);
  bit3 = V8_SMI(*ptr);

  *fast = (bit3 & (1 << cd_v8_bit_field3_dictionary_map_shift)) == 0;

  return cd_ok();
}


cd_error_t cd_v8_get_fixed_arr_len(cd_state_t* state, void* arr, int* size) {
  void** len;

  /* XXX Check type, may be? */
  V8_CORE_PTR(arr, cd_v8_class_FixedArrayBase__length__SMI, len);

  /* We are returning object size, not array size */
  *size = V8_SMI(*len);

  return cd_ok();
}


cd_error_t cd_v8_get_fixed_arr_data(cd_state_t* state,
                                    void* arr,
                                    void** data,
                                    int* size) {
  cd_error_t err;
  void* ptr;

  err = cd_v8_get_fixed_arr_len(state, arr, size);
  if (!cd_is_ok(err))
    return err;

  V8_CORE_DATA(arr,
               cd_v8_class_FixedArray__data__uintptr_t,
               ptr,
               *size * state->ptr_size);
  *data = ptr;

  return cd_ok();
}
