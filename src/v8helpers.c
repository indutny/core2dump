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

    *size *= state->ptr_size;

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
                         int* len,
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
    V8_CORE_PTR(str, cd_v8_class_SeqOneByteString__chars__char, ptr);
    data = (char*) ptr;

    V8_CORE_PTR(str, cd_v8_class_String__length__SMI, ptr);
    if (!V8_IS_SMI(*ptr))
      return cd_error(kCDErrNotString);

    length = V8_SMI(*ptr);
  } else if (repr == cd_v8_ConsStringTag) {
    void* first;
    void* second;
    const char* cfirst;
    const char* csecond;
    int flen;
    int slen;

    V8_CORE_PTR(str, cd_v8_class_ConsString__first__String, ptr);
    first = *ptr;
    V8_CORE_PTR(str, cd_v8_class_ConsString__second__String, ptr);
    second = *ptr;

    err = cd_v8_to_cstr(state, first, &cfirst, &flen, NULL);
    if (!cd_is_ok(err))
      return err;

    err = cd_v8_to_cstr(state, second, &csecond, &slen, NULL);
    if (!cd_is_ok(err))
      return err;

    length = flen + slen;
    if (len != NULL)
      *len = length;

    return cd_strings_concat(&state->strings,
                             res,
                             index,
                             cfirst,
                             flen,
                             csecond,
                             slen);
  } else {
    data = NULL;
    length = 0;
  }

  if (len != NULL)
    *len = length;
  if (data == NULL)
    return cd_error(kCDErrNotFound);

  return cd_strings_copy(&state->strings, res, index, data, length);
}


cd_error_t cd_v8_fn_info(cd_state_t* state,
                         void* fn,
                         const char** res,
                         int* len,
                         int* index,
                         cd_script_t* script) {
  cd_error_t err;
  void** ptr;
  void* sh;
  void* name;
  const char* cname;

  /* Load shared function info to lookup name */
  V8_CORE_PTR(fn, cd_v8_class_JSFunction__shared__SharedFunctionInfo, ptr);
  sh = *ptr;

  V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
  name = *ptr;

  err = cd_v8_to_cstr(state, name, &cname, len, index);
  if (!cd_is_ok(err))
    return err;

  /* Empty name - try inferred name */
  if (cname == NULL || cname[0] == '\0') {
    V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__inferred_name__String, ptr);
    name = *ptr;

    err = cd_v8_to_cstr(state, name, &cname, len, index);
    if (!cd_is_ok(err))
      return err;
  }

  if (res != NULL)
    *res = cname;

  /* Get script info */
  V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__script__Object, ptr);
  if (script == NULL)
    return cd_ok();

  return cd_v8_script_info(state, *ptr, script);
}


cd_error_t cd_v8_script_info(cd_state_t* state, void* obj, cd_script_t* res) {
  void** ptr;
  cd_error_t err;

  res->ptr = obj;
  V8_CORE_PTR(obj, cd_v8_class_Script__name__Object, ptr);
  err = cd_v8_to_cstr(state, *ptr, &res->name, &res->name_len, &res->name_idx);
  if (!cd_is_ok(err))
    return err;

  V8_CORE_PTR(obj, cd_v8_class_Script__id__Smi, ptr);
  if (!V8_IS_SMI(*ptr))
    return cd_error_str(kCDErrNotSMI, "script.id");
  res->id = V8_SMI(*ptr);

  V8_CORE_PTR(obj, cd_v8_class_Script__line_offset__SMI, ptr);
  if (!V8_IS_SMI(*ptr))
    return cd_error_str(kCDErrNotSMI, "script.line");
  res->line = V8_SMI(*ptr);

  V8_CORE_PTR(obj, cd_v8_class_Script__column_offset__SMI, ptr);
  if (!V8_IS_SMI(*ptr))
    return cd_error_str(kCDErrNotSMI, "script.column");
  res->column = V8_SMI(*ptr);

  return cd_ok();
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


cd_error_t cd_v8_obj_has_fast_elems(cd_state_t* state,
                                    void* obj,
                                    void* map,
                                    int* fast) {
  void** ptr;
  int bit2;
  int kind;

  V8_CORE_PTR(map, cd_v8_class_Map__bit_field2__char, ptr);
  bit2 = *(uint8_t*) ptr;

  kind = (bit2 & cd_v8_bit_field2_elements_kind_mask) >>
      cd_v8_bit_field2_elements_kind_shift;
  *fast = kind == cd_v8_elements_fast_elements ||
          kind == cd_v8_elements_fast_holey_elements;
  if (*fast == 0 && kind != cd_v8_elements_dictionary_elements)
    return cd_error(kCDErrUnsupportedElements);

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


cd_error_t cd_v8_is_hole(cd_state_t* state, void* obj, int* is_hole) {
  cd_error_t err;
  int type;
  void** ptr;
  int kind;

  *is_hole = 0;

  if (V8_IS_SMI(obj))
    return cd_ok();

  /* Load object type */
  err = cd_v8_get_obj_type(state, obj, NULL, &type);
  if (!cd_is_ok(err))
    return err;

  if (type != CD_V8_TYPE(Oddball, ODDBALL))
    return cd_ok();

  V8_CORE_PTR(obj, cd_v8_class_Oddball__kind_offset__int, ptr);
  kind = *(uint8_t*) ptr;

  *is_hole = kind == cd_v8_OddballTheHole;

  return cd_ok();
}
