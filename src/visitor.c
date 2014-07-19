#include "visitor.h"
#include "common.h"
#include "error.h"
#include "queue.h"
#include "state.h"
#include "v8helpers.h"
#include "v8constants.h"

#include <stdlib.h>

static cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_queue_range(cd_state_t* state,
                                 cd_node_t* from,
                                 char* start,
                                 char* end);
static cd_error_t cd_add_node(cd_state_t* state,
                              cd_node_t* node,
                              void* map,
                              int type);


cd_error_t cd_visitor_init(cd_state_t* state) {
  QUEUE_INIT(&state->nodes.list);
  state->node_count = 0;
  state->edge_count = 0;

  if (cd_hashmap_init(&state->nodes.map, 1024) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_init(nodes.map)");

  return cd_ok();
}


void cd_visitor_destroy(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->nodes.list)) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->nodes.list);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);
    free(node);
  }

  cd_hashmap_destroy(&state->nodes.map);
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->queue) != 0) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);

    /* Node will be readded to `nodes` in case of success */
    if (!cd_is_ok(cd_visit_root(state, node)))
      free(node);
  }

  return cd_ok();
}


#define T(A, B) CD_V8_TYPE(A, B)


cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node) {
  void** pmap;
  void* map;
  char* start;
  char* end;
  uint8_t* ptype;
  int type;
  cd_error_t err;

  V8_CORE_PTR(node->obj, cd_v8_class_HeapObject__map__Map, pmap);
  map = *pmap;

  if (!V8_IS_HEAPOBJECT(map))
    return cd_error(kCDErrNotObject);

  /* Load object type */
  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, ptype);
  type = *ptype;

  /* Add node to the nodes list as early as possible */
  err = cd_add_node(state, node, map, type);
  if (!cd_is_ok(err))
    return err;

  /* Enqueue map itself */
  err = cd_queue_ptr(state, node, map);
  if (!cd_is_ok(err))
    return err;

  /* Mimique the v8's behaviour, see HeapObject::IterateBody */

  /* Strings... ignore for now */
  if (type < cd_v8_FirstNonstringType)
    return cd_ok();

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
    V8_CORE_PTR(node->obj, off, start);
    V8_CORE_PTR(node->obj, off + size, end);
  } else if (type == T(Map, MAP)) {
    int off;

    /* XXX Map::kPrototypeOffset = Map::kInstanceAttributes + kIntSize */
    off = cd_v8_class_Map__instance_attributes__int + 4;
    V8_CORE_PTR(node->obj, off, start);

    /* Constructor + Prototype */
    V8_CORE_PTR(node->obj, off + state->ptr_size * 2, end);
  } else {
    /* Unknown type - ignore */
    return cd_ok();
  }

  if (start != NULL && end != NULL)
    cd_queue_range(state, node, start, end);

  return cd_ok();
}


cd_error_t cd_queue_ptr(cd_state_t* state, cd_node_t* from, char* ptr) {
  cd_node_t* node;
  cd_edge_t* edge;
  int existing;

  if (!V8_IS_HEAPOBJECT(ptr))
    return cd_error(kCDErrNotObject);

  node = cd_hashmap_get(&state->nodes.map, (const char*) &ptr, sizeof(ptr));
  if (node == NULL) {
    node = malloc(sizeof(*node));
    if (node == NULL)
      return cd_error_str(kCDErrNoMem, "cd_node_t");
    existing = 0;
  } else {
    existing = 1;
  }

  if (from != NULL) {
    edge = malloc(sizeof(*edge));
    if (edge == NULL) {
      if (!existing)
        free(node);
      return cd_error_str(kCDErrNoMem, "cd_edge_t");
    }
  }

  /* Initialize and queue node if just created */
  if (!existing) {
    node->obj = ptr;
    QUEUE_INSERT_TAIL(&state->queue, &node->member);
    QUEUE_INIT(&node->edges);
    node->edge_count = 0;
  }

  /* Fill the edge */

  if (from == NULL)
    return cd_ok();

  from->edge_count++;
  edge->from = from;
  edge->to = node;

  /* TODO(indutny) Figure out theese */
  edge->type = kCDEdgeElement;
  edge->name = 0;

  QUEUE_INSERT_TAIL(&from->edges, &edge->member);
  state->edge_count++;

  return cd_ok();
}


cd_error_t cd_queue_range(cd_state_t* state,
                          cd_node_t* from,
                          char* start,
                          char* end) {
  size_t delta;

  delta = cd_obj_is_x64(state->core) ? 8 : 4;
  for (; start < end; start += delta) {
    cd_error_t err;

    err = cd_queue_ptr(state, from, *(void**) start);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


cd_error_t cd_add_node(cd_state_t* state,
                       cd_node_t* node,
                       void* map,
                       int type) {
  cd_error_t err;
  const char* cname;

  /* Mimique V8HeapExplorer::AddEntry */
  if (type == T(JSFunction, JS_FUNCTION)) {
    void** ptr;
    void* sh;
    void* name;

    /* Load shared function info to lookup name */
    V8_CORE_PTR(node->obj,
                cd_v8_class_JSFunction__shared__SharedFunctionInfo,
                ptr);
    sh = *ptr;

    V8_CORE_PTR(sh, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
    name = *ptr;

    err = cd_v8_to_cstr(state, name, &cname, &node->name);
    if (!cd_is_ok(err))
      return err;

    node->type = kCDNodeClosure;
  } else {
    err = cd_strings_copy(&state->strings, &cname, &node->name, "", 0);
    if (!cd_is_ok(err))
      return err;

    node->type = kCDNodeHidden;
  }

  err = cd_v8_get_obj_size(state, map, type, &node->size);
  if (!cd_is_ok(err))
    return err;

  node->id = state->node_count++;

  if (cd_hashmap_insert(&state->nodes.map,
                        (const char*) &node->obj,
                        sizeof(node->obj),
                        node) != 0) {
    return cd_error_str(kCDErrNoMem, "cd_hashmap_insert(nodes.map)");
  }

  QUEUE_INSERT_TAIL(&state->nodes.list, &node->member);

  return cd_ok();
}


#undef T
