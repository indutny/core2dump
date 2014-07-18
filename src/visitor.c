#include "visitor.h"
#include "common.h"
#include "error.h"
#include "state.h"
#include "v8helpers.h"
#include "v8constants.h"

#include <stdlib.h>

static cd_error_t cd_visit_root(cd_state_t* state, void* obj);
static cd_error_t cd_queue_ptr(cd_state_t* state, char* ptr);
static cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end);
static cd_error_t cd_add_node(cd_state_t* state, void* obj, int type);


cd_error_t cd_visitor_init(cd_state_t* state) {
  if (cd_list_init(&state->queue, 32, sizeof(void*)) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_t state->queue");

  if (cd_list_init(&state->nodes, 32, sizeof(cd_node_t)) != 0) {
    cd_list_free(&state->queue);
    return cd_error_str(kCDErrNoMem, "cd_list_t state->queue");
  }

  return cd_ok();
}


void cd_visitor_destroy(cd_state_t* state) {
  cd_list_free(&state->queue);
  cd_list_free(&state->nodes);
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  while (cd_list_len(&state->queue) != 0) {
    void* ptr;

    if (cd_list_shift(&state->queue, &ptr) != 0)
      return cd_error(kCDErrListShift);

    cd_visit_root(state, ptr);
  }

  return cd_ok();
}


#define T(M, S) cd_v8_type_##M##__##S##_TYPE


cd_error_t cd_visit_root(cd_state_t* state, void* obj) {
  void** pmap;
  void* map;
  char* start;
  char* end;
  uint8_t* ptype;
  int type;
  cd_error_t err;

  V8_CORE_PTR(obj, cd_v8_class_HeapObject__map__Map, pmap);

  /* If zapped - the node was already added */
  map = *pmap;
  if (((intptr_t) map & state->zap_bit) == state->zap_bit)
    return cd_ok();
  *pmap = (void*) ((intptr_t) map | state->zap_bit);

  /* Enqueue map itself */
  err = cd_queue_ptr(state, map);
  if (!cd_is_ok(err))
    return err;

  /* Load object type */
  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, ptype);
  type = *ptype;

  err = cd_add_node(state, obj, type);
  if (!cd_is_ok(err))
    return err;

  /* Mimique the v8's behaviour, see HeapObject::IterateBody */

  if (type < cd_v8_FirstNonstringType) {
    /* Strings... ignore for now */
    return cd_ok();
  }

  start = NULL;
  end = NULL;

  if (type == T(JSObject, JS_OBJECT) ||
      type == T(JSValue, JS_VALUE) ||
      type == T(JSDate, JS_DATE) ||
      type == T(JSArray, JS_ARRAY) ||
      type == T(JSArrayBuffer, JS_ARRAY_BUFFER) ||
      type == T(JSTypedArray, JS_TYPED_ARRAY) ||
      type == T(JSDataView, JS_DATA_VIEW) ||
      type == T(JSRegExp, JS_REGEXP) ||
      type == T(JSGlobalObject, JS_GLOBAL_OBJECT) ||
      type == T(JSBuiltinsObject, JS_BUILTINS_OBJECT) ||
      type == T(JSMessageObject, JS_MESSAGE_OBJECT) ||
      /* NOTE: Function has non-heap fields, but who cares! */
      type == T(JSFunction, JS_FUNCTION)) {
    /* General object */
    int size;
    int off;

    err = cd_v8_get_obj_size(state, map, type, &size);
    if (!cd_is_ok(err))
      return err;

    off = cd_v8_class_JSObject__properties__FixedArray;
    V8_CORE_PTR(obj, off, start);
    V8_CORE_PTR(obj, off + size, end);
  } else if (type == T(Map, MAP)) {
    int off;

    /* XXX Map::kPrototypeOffset = Map::kInstanceAttributes + kIntSize */
    off = cd_v8_class_Map__instance_attributes__int + 4;
    V8_CORE_PTR(obj, off, start);

    /* Constructor + Prototype */
    V8_CORE_PTR(obj, off + state->ptr_size * 2, end);
  } else {
    /* Unknown type - ignore */
    return cd_ok();
  }

  if (start != NULL && end != NULL)
    err = cd_queue_space(state, start, end);

  return err;
}


cd_error_t cd_queue_ptr(cd_state_t* state, char* ptr) {
  if (!V8_IS_HEAPOBJECT(ptr))
    return cd_error(kCDErrNotObject);

  if (cd_list_push(&state->queue, &ptr) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push queue space");

  return cd_ok();
}


cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end) {
  size_t delta;

  delta = cd_obj_is_x64(state->core) ? 8 : 4;
  for (; start < end; start += delta) {
    cd_error_t err;

    err = cd_queue_ptr(state, *(void**) start);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


#undef T


cd_error_t cd_add_node(cd_state_t* state, void* obj, int type) {
  cd_node_t node;

  node.obj = obj;
  node.type = type;

  /* Push node */
  if (cd_list_push(&state->nodes, &node) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push nodes");

  return cd_ok();
}
