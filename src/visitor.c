#include "visitor.h"
#include "collector.h"
#include "common.h"
#include "error.h"
#include "queue.h"
#include "state.h"
#include "v8helpers.h"
#include "v8constants.h"

#include <stdlib.h>

static cd_error_t cd_visit_root(cd_state_t* state, void* obj);
static cd_error_t cd_queue_ptr(cd_state_t* state, char* ptr);
static cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end);
static cd_error_t cd_add_node(cd_state_t* state,
                              void* obj,
                              void* map,
                              int type);


cd_error_t cd_visitor_init(cd_state_t* state) {
  QUEUE_INIT(&state->nodes);
  state->node_count = 0;

  return cd_ok();
}


void cd_visitor_destroy(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->nodes)) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->nodes);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);
    free(node);
  }
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->queue) != 0) {
    QUEUE* q;
    cd_collect_item_t* item;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);
    item = container_of(q, cd_collect_item_t, member);

    cd_visit_root(state, item->obj);
    free(item);
  }

  return cd_ok();
}


#define T(A, B) CD_V8_TYPE(A, B)


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

  err = cd_add_node(state, obj, map, type);
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
  cd_collect_item_t* item;

  if (!V8_IS_HEAPOBJECT(ptr))
    return cd_error(kCDErrNotObject);

  item = malloc(sizeof(*item));
  if (item == NULL)
    return cd_error_str(kCDErrNoMem, "queue ptr");

  item->obj = ptr;
  QUEUE_INSERT_TAIL(&state->queue, &item->member);

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


cd_error_t cd_add_node(cd_state_t* state, void* obj, void* map, int type) {
  cd_error_t err;
  cd_node_t* node;
  const char* cname;
  int name;
  cd_node_type_t ntype;
  int size;

  /* Mimique V8HeapExplorer::AddEntry */
  if (type == T(JSFunction, JS_FUNCTION)) {
    void** ptr;
    void* sh;
    void* sname;

    /* Load shared function info to lookup name */
    V8_CORE_PTR(obj, cd_v8_class_JSFunction__shared__SharedFunctionInfo, ptr);
    sh = *ptr;

    V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
    sname = *ptr;

    err = cd_v8_to_cstr(state, sname, &cname, &name);
    if (!cd_is_ok(err))
      return err;

    ntype = kCDNodeClosure;
  } else {
    err = cd_strings_copy(&state->strings, &cname, &name, "", 0);
    if (!cd_is_ok(err))
      return err;

    ntype = kCDNodeHidden;
  }

  err = cd_v8_get_obj_size(state, map, type, &size);
  if (!cd_is_ok(err))
    return err;

  node = malloc(sizeof(*node));
  if (node == NULL)
    return cd_error_str(kCDErrNoMem, "cd_node_t");

  node->id = state->node_count++;
  node->name = name;
  node->type = ntype;
  node->size = size;

  QUEUE_INSERT_TAIL(&state->nodes, &node->member);

  return cd_ok();
}


#undef T
