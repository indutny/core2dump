#ifndef SRC_V8_CONSTANTS_H_
#define SRC_V8_CONSTANTS_H_

#include "error.h"
#include "obj.h"

/* node.js v0.10 default values */
#include "node/node-0-10-ia32.h"

static const int kCDV8RegExpPattern = 1;
static const int kCDV8MapFieldOffset = 4;
static const int kCDV8MapFieldCount = 2;

#define CD_V8_REQUIRED_CONSTANTS_ENUM(X)                                      \
    X(AsciiStringTag, V8DBG_ASCIISTRINGTAG)                                   \
    X(ConsStringTag, V8DBG_CONSSTRINGTAG)                                     \
    X(ExternalStringTag, V8DBG_EXTERNALSTRINGTAG)                             \
    X(FailureTag, V8DBG_FAILURETAG)                                           \
    X(FailureTagMask, V8DBG_FAILURETAGMASK)                                   \
    X(FirstNonstringType, V8DBG_FIRSTNONSTRINGTYPE)                           \
    X(HeapObjectTag, V8DBG_HEAPOBJECTTAG)                                     \
    X(HeapObjectTagMask, V8DBG_HEAPOBJECTTAGMASK)                             \
    X(IsNotStringMask, V8DBG_ISNOTSTRINGMASK)                                 \
    X(NotStringTag, V8DBG_NOTSTRINGTAG)                                       \
    X(PointerSizeLog2, V8DBG_POINTERSIZELOG2)                                 \
    X(SeqStringTag, V8DBG_SEQSTRINGTAG)                                       \
    X(ConsStringTag, V8DBG_CONSSTRINGTAG)                                     \
    X(SmiShiftSize, V8DBG_SMISHIFTSIZE)                                       \
    X(SmiTag, V8DBG_SMITAG)                                                   \
    X(SmiTagMask, V8DBG_SMITAGMASK)                                           \
    X(SmiValueShift, V8DBG_SMIVALUESHIFT)                                     \
    X(StringEncodingMask, V8DBG_STRINGENCODINGMASK)                           \
    X(StringRepresentationMask, V8DBG_STRINGREPRESENTATIONMASK)               \
    X(StringTag, V8DBG_STRINGTAG)                                             \
    X(TwoByteStringTag, V8DBG_TWOBYTESTRINGTAG)                               \
    X(class_ConsString__first__String, V8DBG_CLASS_CONSSTRING__FIRST__STRING) \
    X(class_ConsString__second__String,                                       \
      V8DBG_CLASS_CONSSTRING__SECOND__STRING)                                 \
    X(class_ExternalString__resource__Object,                                 \
      V8DBG_CLASS_EXTERNALSTRING__RESOURCE__OBJECT)                           \
    X(class_FixedArrayBase__length__SMI,                                      \
      V8DBG_CLASS_FIXEDARRAYBASE__LENGTH__SMI)                                \
    X(class_FixedArray__data__uintptr_t,                                      \
      V8DBG_CLASS_FIXEDARRAY__DATA__UINTPTR_T)                                \
    X(class_FreeSpace__size__SMI, V8DBG_CLASS_FREESPACE__SIZE__SMI)           \
    X(class_GlobalObject__builtins__JSBuiltinsObject,                         \
      V8DBG_CLASS_GLOBALOBJECT__BUILTINS__JSBUILTINSOBJECT)                   \
    X(class_GlobalObject__global_context__Context,                            \
      V8DBG_CLASS_GLOBALOBJECT__GLOBAL_CONTEXT__CONTEXT)                      \
    X(class_GlobalObject__global_receiver__JSObject,                          \
      V8DBG_CLASS_GLOBALOBJECT__GLOBAL_RECEIVER__JSOBJECT)                    \
    X(class_GlobalObject__native_context__Context,                            \
      V8DBG_CLASS_GLOBALOBJECT__NATIVE_CONTEXT__CONTEXT)                      \
    X(class_HeapNumber__value__double, V8DBG_CLASS_HEAPNUMBER__VALUE__DOUBLE) \
    X(class_HeapObject__map__Map, V8DBG_CLASS_HEAPOBJECT__MAP__MAP)           \
    X(class_JSObject__elements__Object,                                       \
      V8DBG_CLASS_JSOBJECT__ELEMENTS__OBJECT)                                 \
    X(class_JSArray__length__Object, V8DBG_CLASS_JSARRAY__LENGTH__OBJECT)     \
    X(class_JSFunction__literals_or_bindings__FixedArray,                     \
      V8DBG_CLASS_JSFUNCTION__LITERALS_OR_BINDINGS__FIXEDARRAY)               \
    X(class_JSFunction__next_function_link__Object,                           \
      V8DBG_CLASS_JSFUNCTION__NEXT_FUNCTION_LINK__OBJECT)                     \
    X(class_JSFunction__prototype_or_initial_map__Object,                     \
      V8DBG_CLASS_JSFUNCTION__PROTOTYPE_OR_INITIAL_MAP__OBJECT)               \
    X(class_JSFunction__shared__SharedFunctionInfo,                           \
      V8DBG_CLASS_JSFUNCTION__SHARED__SHAREDFUNCTIONINFO)                     \
    X(class_JSMap__table__Object, V8DBG_CLASS_JSMAP__TABLE__OBJECT)           \
    X(class_JSObject__properties__FixedArray,                                 \
      V8DBG_CLASS_JSOBJECT__PROPERTIES__FIXEDARRAY)                           \
    X(class_Map__bit_field3__SMI, V8DBG_CLASS_MAP__BIT_FIELD3__SMI)           \
    X(class_Map__code_cache__Object, V8DBG_CLASS_MAP__CODE_CACHE__OBJECT)     \
    X(class_Map__constructor__Object, V8DBG_CLASS_MAP__CONSTRUCTOR__OBJECT)   \
    X(class_Map__inobject_properties__int,                                    \
      V8DBG_CLASS_MAP__INOBJECT_PROPERTIES__INT)                              \
    X(class_Map__instance_attributes__int,                                    \
      V8DBG_CLASS_MAP__INSTANCE_ATTRIBUTES__INT)                              \
    X(class_Map__instance_descriptors__DescriptorArray,                       \
      V8DBG_CLASS_MAP__INSTANCE_DESCRIPTORS__DESCRIPTORARRAY)                 \
    X(class_Map__instance_size__int, V8DBG_CLASS_MAP__INSTANCE_SIZE__INT)     \
    X(class_Script__source__Object, V8DBG_CLASS_SCRIPT__SOURCE__OBJECT)       \
    X(class_Script__name__Object, V8DBG_CLASS_SCRIPT__NAME__OBJECT)           \
    X(class_Script__context_data__Object,                                     \
      V8DBG_CLASS_SCRIPT__CONTEXT_DATA__OBJECT)                               \
    X(class_SharedFunctionInfo__inferred_name__String,                        \
      V8DBG_CLASS_SHAREDFUNCTIONINFO__INFERRED_NAME__STRING)                  \
    X(class_SharedFunctionInfo__name__Object,                                 \
      V8DBG_CLASS_SHAREDFUNCTIONINFO__NAME__OBJECT)                           \
    X(class_SharedFunctionInfo__script__Object,                               \
      V8DBG_CLASS_SHAREDFUNCTIONINFO__SCRIPT__OBJECT)                         \
    X(class_String__length__SMI, V8DBG_CLASS_STRING__LENGTH__SMI)             \
    X(class_JSRegExp__data__Object, V8DBG_CLASS_JSREGEXP__DATA__OBJECT)       \
    X(frametype_ArgumentsAdaptorFrame, V8DBG_FRAMETYPE_ARGUMENTSADAPTORFRAME) \
    X(frametype_ConstructFrame, V8DBG_FRAMETYPE_CONSTRUCTFRAME)               \
    X(frametype_EntryConstructFrame, V8DBG_FRAMETYPE_ENTRYCONSTRUCTFRAME)     \
    X(frametype_EntryFrame, V8DBG_FRAMETYPE_ENTRYFRAME)                       \
    X(frametype_ExitFrame, V8DBG_FRAMETYPE_EXITFRAME)                         \
    X(frametype_InternalFrame, V8DBG_FRAMETYPE_INTERNALFRAME)                 \
    X(frametype_JavaScriptFrame, V8DBG_FRAMETYPE_JAVASCRIPTFRAME)             \
    X(frametype_OptimizedFrame, V8DBG_FRAMETYPE_OPTIMIZEDFRAME)               \
    X(off_fp_args, V8DBG_OFF_FP_ARGS)                                         \
    X(off_fp_context, V8DBG_OFF_FP_CONTEXT)                                   \
    X(off_fp_function, V8DBG_OFF_FP_FUNCTION)                                 \
    X(off_fp_marker, V8DBG_OFF_FP_MARKER)                                     \
    X(parent_ConsString__String, V8DBG_PARENT_CONSSTRING__STRING)             \
    X(prop_desc_details, V8DBG_PROP_DESC_DETAILS)                             \
    X(prop_desc_key, V8DBG_PROP_DESC_KEY)                                     \
    X(prop_desc_size, V8DBG_PROP_DESC_SIZE)                                   \
    X(prop_desc_value, V8DBG_PROP_DESC_VALUE)                                 \
    X(prop_idx_first, V8DBG_PROP_IDX_FIRST)                                   \
    X(prop_type_field, V8DBG_PROP_TYPE_FIELD)                                 \
    X(prop_type_first_phantom, V8DBG_PROP_TYPE_FIRST_PHANTOM)                 \
    X(prop_type_mask, V8DBG_PROP_TYPE_MASK)                                   \
    X(type_AccessorPair__ACCESSOR_PAIR_TYPE,                                  \
      V8DBG_TYPE_ACCESSORPAIR__ACCESSOR_PAIR_TYPE)                            \
    X(type_AccessCheckInfo__ACCESS_CHECK_INFO_TYPE,                           \
      V8DBG_TYPE_ACCESSCHECKINFO__ACCESS_CHECK_INFO_TYPE)                     \
    X(type_AliasedArgumentsEntry__ALIASED_ARGUMENTS_ENTRY_TYPE,               \
      V8DBG_TYPE_ALIASEDARGUMENTSENTRY__ALIASED_ARGUMENTS_ENTRY_TYPE)         \
    X(type_BreakPointInfo__BREAK_POINT_INFO_TYPE,                             \
      V8DBG_TYPE_BREAKPOINTINFO__BREAK_POINT_INFO_TYPE)                       \
    X(type_ByteArray__BYTE_ARRAY_TYPE, V8DBG_TYPE_BYTEARRAY__BYTE_ARRAY_TYPE) \
    X(type_CallHandlerInfo__CALL_HANDLER_INFO_TYPE,                           \
      V8DBG_TYPE_CALLHANDLERINFO__CALL_HANDLER_INFO_TYPE)                     \
    X(type_CodeCache__CODE_CACHE_TYPE, V8DBG_TYPE_CODECACHE__CODE_CACHE_TYPE) \
    X(type_Code__CODE_TYPE, V8DBG_TYPE_CODE__CODE_TYPE)                       \
    X(type_ConsString__CONS_ASCII_STRING_TYPE,                                \
      V8DBG_TYPE_CONSSTRING__CONS_ASCII_STRING_TYPE)                          \
    X(type_ConsString__CONS_STRING_TYPE,                                      \
      V8DBG_TYPE_CONSSTRING__CONS_STRING_TYPE)                                \
    X(type_DebugInfo__DEBUG_INFO_TYPE, V8DBG_TYPE_DEBUGINFO__DEBUG_INFO_TYPE) \
    X(type_ExternalAsciiString__EXTERNAL_ASCII_STRING_TYPE,                   \
      V8DBG_TYPE_EXTERNALASCIISTRING__EXTERNAL_ASCII_STRING_TYPE)             \
    X(type_ExternalTwoByteString__EXTERNAL_STRING_TYPE,                       \
      V8DBG_TYPE_EXTERNALTWOBYTESTRING__EXTERNAL_STRING_TYPE)                 \
    X(type_FixedArray__FIXED_ARRAY_TYPE,                                      \
      V8DBG_TYPE_FIXEDARRAY__FIXED_ARRAY_TYPE)                                \
    X(type_FixedDoubleArray__FIXED_DOUBLE_ARRAY_TYPE,                         \
      V8DBG_TYPE_FIXEDDOUBLEARRAY__FIXED_DOUBLE_ARRAY_TYPE)                   \
    X(type_Foreign__FOREIGN_TYPE, V8DBG_TYPE_FOREIGN__FOREIGN_TYPE)           \
    X(type_FreeSpace__FREE_SPACE_TYPE, V8DBG_TYPE_FREESPACE__FREE_SPACE_TYPE) \
    X(type_FunctionTemplateInfo__FUNCTION_TEMPLATE_INFO_TYPE,                 \
      V8DBG_TYPE_FUNCTIONTEMPLATEINFO__FUNCTION_TEMPLATE_INFO_TYPE)           \
    X(type_HeapNumber__HEAP_NUMBER_TYPE,                                      \
      V8DBG_TYPE_HEAPNUMBER__HEAP_NUMBER_TYPE)                                \
    X(type_InterceptorInfo__INTERCEPTOR_INFO_TYPE,                            \
      V8DBG_TYPE_INTERCEPTORINFO__INTERCEPTOR_INFO_TYPE)                      \
    X(type_JSArray__JS_ARRAY_TYPE, V8DBG_TYPE_JSARRAY__JS_ARRAY_TYPE)         \
    X(type_JSBuiltinsObject__JS_BUILTINS_OBJECT_TYPE,                         \
      V8DBG_TYPE_JSBUILTINSOBJECT__JS_BUILTINS_OBJECT_TYPE)                   \
    X(type_JSDate__JS_DATE_TYPE, V8DBG_TYPE_JSDATE__JS_DATE_TYPE)             \
    X(type_JSFunctionProxy__JS_FUNCTION_PROXY_TYPE,                           \
      V8DBG_TYPE_JSFUNCTIONPROXY__JS_FUNCTION_PROXY_TYPE)                     \
    X(type_JSFunction__JS_FUNCTION_TYPE,                                      \
      V8DBG_TYPE_JSFUNCTION__JS_FUNCTION_TYPE)                                \
    X(type_JSGlobalObject__JS_GLOBAL_OBJECT_TYPE,                             \
      V8DBG_TYPE_JSGLOBALOBJECT__JS_GLOBAL_OBJECT_TYPE)                       \
    X(type_JSMap__JS_MAP_TYPE, V8DBG_TYPE_JSMAP__JS_MAP_TYPE)                 \
    X(type_JSMessageObject__JS_MESSAGE_OBJECT_TYPE,                           \
      V8DBG_TYPE_JSMESSAGEOBJECT__JS_MESSAGE_OBJECT_TYPE)                     \
    X(type_JSModule__JS_MODULE_TYPE, V8DBG_TYPE_JSMODULE__JS_MODULE_TYPE)     \
    X(type_JSObject__JS_OBJECT_TYPE, V8DBG_TYPE_JSOBJECT__JS_OBJECT_TYPE)     \
    X(type_JSProxy__JS_PROXY_TYPE, V8DBG_TYPE_JSPROXY__JS_PROXY_TYPE)         \
    X(type_JSRegExp__JS_REGEXP_TYPE, V8DBG_TYPE_JSREGEXP__JS_REGEXP_TYPE)     \
    X(type_JSSet__JS_SET_TYPE, V8DBG_TYPE_JSSET__JS_SET_TYPE)                 \
    X(type_JSValue__JS_VALUE_TYPE, V8DBG_TYPE_JSVALUE__JS_VALUE_TYPE)         \
    X(type_JSWeakMap__JS_WEAK_MAP_TYPE,                                       \
      V8DBG_TYPE_JSWEAKMAP__JS_WEAK_MAP_TYPE)                                 \
    X(type_Map__MAP_TYPE, V8DBG_TYPE_MAP__MAP_TYPE)                           \
    X(type_ObjectTemplateInfo__OBJECT_TEMPLATE_INFO_TYPE,                     \
      V8DBG_TYPE_OBJECTTEMPLATEINFO__OBJECT_TEMPLATE_INFO_TYPE)               \
    X(type_Oddball__ODDBALL_TYPE, V8DBG_TYPE_ODDBALL__ODDBALL_TYPE)           \
    X(type_PolymorphicCodeCache__POLYMORPHIC_CODE_CACHE_TYPE,                 \
      V8DBG_TYPE_POLYMORPHICCODECACHE__POLYMORPHIC_CODE_CACHE_TYPE)           \
    X(type_Script__SCRIPT_TYPE, V8DBG_TYPE_SCRIPT__SCRIPT_TYPE)               \
    X(type_SharedFunctionInfo__SHARED_FUNCTION_INFO_TYPE,                     \
      V8DBG_TYPE_SHAREDFUNCTIONINFO__SHARED_FUNCTION_INFO_TYPE)               \
    X(type_SignatureInfo__SIGNATURE_INFO_TYPE,                                \
      V8DBG_TYPE_SIGNATUREINFO__SIGNATURE_INFO_TYPE)                          \
    X(type_SeqTwoByteString__STRING_TYPE,                                     \
      V8DBG_TYPE_SEQTWOBYTESTRING__STRING_TYPE)                               \
    X(type_SeqTwoByteString__SYMBOL_TYPE,                                     \
      V8DBG_TYPE_SEQTWOBYTESTRING__SYMBOL_TYPE)                               \
    X(type_TypeFeedbackInfo__TYPE_FEEDBACK_INFO_TYPE,                         \
      V8DBG_TYPE_TYPEFEEDBACKINFO__TYPE_FEEDBACK_INFO_TYPE)                   \
    X(type_TypeSwitchInfo__TYPE_SWITCH_INFO_TYPE,                             \
      V8DBG_TYPE_TYPESWITCHINFO__TYPE_SWITCH_INFO_TYPE)                       \

#define CD_V8_OPTIONAL_CONSTANTS_ENUM(X)                                      \
    X(SlicedStringTag, -1)                                                    \
    X(class_SlicedString__offset__SMI, -1)                                    \
    X(type_SlicedString__SLICED_ASCII_STRING_TYPE, -1)                        \
    X(type_SlicedString__SLICED_STRING_TYPE, -1)                              \
    X(class_Map__dependent_code__DependentCode, -1)                           \
    X(class_StringDictionaryShape__prefix_size__int,                          \
      V8DBG_CLASS_STRINGDICTIONARYSHAPE__PREFIX_SIZE__INT)                    \
    X(class_StringDictionaryShape__entry_size__int,                           \
      V8DBG_CLASS_STRINGDICTIONARYSHAPE__ENTRY_SIZE__INT)                     \
    X(class_NameDictionaryShape__prefix_size__int,                            \
      cd_v8_class_StringDictionaryShape__prefix_size__int)                    \
    X(class_NameDictionaryShape__entry_size__int,                             \
      cd_v8_class_StringDictionaryShape__entry_size__int)                     \
    X(class_SeqAsciiString__chars__char, -1)                                  \
    X(type_SeqAsciiString__ASCII_STRING_TYPE, -1)                             \
    X(class_SeqOneByteString__chars__char,                                    \
      cd_v8_class_SeqAsciiString__chars__char)                                \
    X(type_SeqOneByteString__ASCII_STRING_TYPE,                               \
      cd_v8_type_SeqAsciiString__ASCII_STRING_TYPE)                           \
    X(class_SeqTwoByteString__chars__char,                                    \
      cd_v8_class_SeqOneByteString__chars__char)                              \
    X(class_Script__id__Object, -1)                                           \
    X(class_Script__id__Smi,                                                  \
      cd_v8_class_Script__id__Object)                                         \
    /* node.js v0.10 defaults */                                              \
    X(prop_index_mask, 0x7ff80)                                               \
    X(prop_index_shift, 7)                                                    \
    X(type_JSArrayBuffer__JS_ARRAY_BUFFER_TYPE, -1)                           \
    X(type_JSGeneratorObject__JS_GENERATOR_OBJECT_TYPE, -1)                   \
    X(type_JSTypedArray__JS_TYPED_ARRAY_TYPE, -1)                             \
    X(type_JSWeakSet__JS_WEAK_SET_TYPE, -1)                                   \
    X(type_PropertyCell__PROPERTY_CELL_TYPE, -1)                              \
    X(OddballTheHole, V8DBG_ODDBALLTHEHOLE)                                   \
    X(class_Map__bit_field2__char,                                            \
      cd_v8_class_Map__instance_attributes__int + 3)                          \
    X(class_Map__prototype__Object,                                           \
      cd_v8_class_Map__instance_attributes__int + 4)                          \
    X(class_SeededNumberDictionaryShape__prefix_size__int,                    \
      V8DBG_CLASS_SEEDEDNUMBERDICTIONARYSHAPE__PREFIX_SIZE__INT)              \
    X(class_UnseededNumberDictionaryShape__prefix_size__int,                  \
      V8DBG_CLASS_UNSEEDEDNUMBERDICTIONARYSHAPE__PREFIX_SIZE__INT)            \
    X(class_NumberDictionaryShape__entry_size__int,                           \
      V8DBG_CLASS_NUMBERDICTIONARYSHAPE__ENTRY_SIZE__INT)                     \
    X(class_Oddball__kind_offset__int,                                        \
      V8DBG_CLASS_ODDBALL__KIND_OFFSET__INT)                                  \
    X(class_Script__line_offset__SMI,                                         \
      V8DBG_CLASS_SCRIPT__LINE_OFFSET__SMI)                                   \
    X(class_Script__column_offset__SMI,                                       \
      V8DBG_CLASS_SCRIPT__COLUMN_OFFSET__SMI)                                 \
    X(elements_fast_holey_elements, V8DBG_ELEMENTS_FAST_HOLEY_ELEMENTS)       \
    X(elements_fast_elements, V8DBG_ELEMENTS_FAST_ELEMENTS)                   \
    X(elements_dictionary_elements, V8DBG_ELEMENTS_DICTIONARY_ELEMENTS)       \
    X(bit_field2_elements_kind_mask, V8DBG_BIT_FIELD2_ELEMENTS_KIND_MASK)     \
    X(bit_field2_elements_kind_shift, V8DBG_BIT_FIELD2_ELEMENTS_KIND_SHIFT)   \
    X(bit_field3_dictionary_map_shift, V8DBG_BIT_FIELD3_DICTIONARY_MAP_SHIFT) \

#define CD_V8_CONSTANT_VALUE(V, D) int cd_v8_##V;
CD_V8_REQUIRED_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
CD_V8_OPTIONAL_CONSTANTS_ENUM(CD_V8_CONSTANT_VALUE);
#undef CD_V8_CONSTANT_VALUE

#define CD_V8_TYPE(M, S) cd_v8_type_##M##__##S##_TYPE

cd_error_t cd_v8_init(cd_obj_t* core);

#endif  /* SRC_V8_CONSTANTS_H_ */
