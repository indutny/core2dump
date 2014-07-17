#ifndef SRC_V8_CONSTANTS_H_
#define SRC_V8_CONSTANTS_H_

#include "error.h"
#include "obj.h"

#define CD_V8_CONSTANTS_ENUM(X)                                               \
    X(AsciiStringTag)                                                         \
    X(ConsStringTag)                                                          \
    X(ExternalStringTag)                                                      \
    X(FailureTag)                                                             \
    X(FailureTagMask)                                                         \
    X(FirstNonstringType)                                                     \
    X(HeapObjectTag)                                                          \
    X(HeapObjectTagMask)                                                      \
    X(IsNotStringMask)                                                        \
    X(NotStringTag)                                                           \
    X(PointerSizeLog2)                                                        \
    X(SeqStringTag)                                                           \
    X(SmiShiftSize)                                                           \
    X(SmiTag)                                                                 \
    X(SmiTagMask)                                                             \
    X(SmiValueShift)                                                          \
    X(StringEncodingMask)                                                     \
    X(StringRepresentationMask)                                               \
    X(StringTag)                                                              \
    X(TwoByteStringTag)                                                       \
    X(class_ConsString__first__String)                                        \
    X(class_ConsString__second__String)                                       \
    X(class_ExternalString__resource__Object)                                 \
    X(class_FixedArrayBase__length__SMI)                                      \
    X(class_FixedArray__data__uintptr_t)                                      \
    X(class_FreeSpace__size__SMI)                                             \
    X(class_GlobalObject__builtins__JSBuiltinsObject)                         \
    X(class_GlobalObject__global_context__Context)                            \
    X(class_GlobalObject__global_receiver__JSObject)                          \
    X(class_GlobalObject__native_context__Context)                            \
    X(class_HeapNumber__value__double)                                        \
    X(class_HeapObject__map__Map)                                             \
    X(class_JSArray__length__Object)                                          \
    X(class_JSFunction__literals_or_bindings__FixedArray)                     \
    X(class_JSFunction__next_function_link__Object)                           \
    X(class_JSFunction__prototype_or_initial_map__Object)                     \
    X(class_JSFunction__shared__SharedFunctionInfo)                           \
    X(class_JSMap__table__Object)                                             \
    X(class_Map__code_cache__Object)                                          \
    X(class_Map__constructor__Object)                                         \
    X(class_Map__inobject_properties__int)                                    \
    X(class_Map__instance_attributes__int)                                    \
    X(class_Map__instance_descriptors__DescriptorArray)                       \
    X(class_Map__instance_size__int)                                          \
    X(class_SharedFunctionInfo__inferred_name__String)                        \
    X(class_SharedFunctionInfo__name__Object)                                 \
    X(class_SharedFunctionInfo__script__Object)                               \
    X(class_SlicedString__offset__SMI)                                        \
    X(class_String__length__SMI)                                              \
    X(frametype_ArgumentsAdaptorFrame)                                        \
    X(frametype_ConstructFrame)                                               \
    X(frametype_EntryConstructFrame)                                          \
    X(frametype_EntryFrame)                                                   \
    X(frametype_ExitFrame)                                                    \
    X(frametype_InternalFrame)                                                \
    X(frametype_JavaScriptFrame)                                              \
    X(frametype_OptimizedFrame)                                               \
    X(off_fp_args)                                                            \
    X(off_fp_context)                                                         \
    X(off_fp_function)                                                        \
    X(off_fp_marker)                                                          \
    X(parent_ConsString__String)                                              \
    X(prop_desc_details)                                                      \
    X(prop_desc_key)                                                          \
    X(prop_desc_size)                                                         \
    X(prop_desc_value)                                                        \
    X(prop_idx_first)                                                         \
    X(prop_type_field)                                                        \
    X(prop_type_first_phantom)                                                \
    X(prop_type_mask)                                                         \
    X(type_Code__CODE_TYPE)                                                   \
    X(type_ConsString__CONS_ASCII_STRING_TYPE)                                \
    X(type_ConsString__CONS_STRING_TYPE)                                      \
    X(type_ExternalAsciiString__EXTERNAL_ASCII_STRING_TYPE)                   \
    X(type_HeapNumber__HEAP_NUMBER_TYPE)                                      \
    X(type_JSArray__JS_ARRAY_TYPE)                                            \
    X(type_JSBuiltinsObject__JS_BUILTINS_OBJECT_TYPE)                         \
    X(type_JSDate__JS_DATE_TYPE)                                              \
    X(type_JSFunctionProxy__JS_FUNCTION_PROXY_TYPE)                           \
    X(type_JSFunction__JS_FUNCTION_TYPE)                                      \
    X(type_JSGlobalObject__JS_GLOBAL_OBJECT_TYPE)                             \
    X(type_JSMap__JS_MAP_TYPE)                                                \
    X(type_JSObject__JS_OBJECT_TYPE)                                          \
    X(type_Map__MAP_TYPE)                                                     \
    X(type_ObjectTemplateInfo__OBJECT_TEMPLATE_INFO_TYPE)                     \
    X(type_Script__SCRIPT_TYPE)                                               \
    X(type_SeqTwoByteString__STRING_TYPE)                                     \
    X(type_SharedFunctionInfo__SHARED_FUNCTION_INFO_TYPE)                     \
    X(type_SlicedString__SLICED_ASCII_STRING_TYPE)                            \
    X(type_SlicedString__SLICED_STRING_TYPE)                                  \

#define CD_V8_CONSTANT_VALUE(V) int cd_v8_##V;
CD_V8_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
#undef CD_V8_CONSTANT_VALUE

cd_error_t cd_v8_init(cd_obj_t* binary, cd_obj_t* core);

#endif  /* SRC_V8_CONSTANTS_H_ */
