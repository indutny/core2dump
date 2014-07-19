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
static cd_error_t cd_add_node(cd_state_t* state, cd_node_t* node, int type);


cd_error_t cd_visitor_init(cd_state_t* state) {
  cd_error_t err;
  cd_node_t* root;
  const char* ptr;

  QUEUE_INIT(&state->nodes.list);

  state->node_count = 0;
  state->edge_count = 0;

  /* Init root and insert it */
  root = &state->nodes.root;
  QUEUE_INSERT_TAIL(&state->nodes.list, &root->member);
  root->type = kCDNodeSynthetic;
  root->id = state->node_count++;
  root->size = 0;
  QUEUE_INIT(&root->edges);
  root->edge_count = 0;

  err = cd_strings_copy(&state->strings, &ptr, &root->name, "<root>", 6);
  if (!cd_is_ok(err))
    return err;

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
    if (node != &state->nodes.root)
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
  cd_error_t err;
  void** pmap;
  void* map;
  char* start;
  char* end;
  int type;

  V8_CORE_PTR(node->obj, cd_v8_class_HeapObject__map__Map, pmap);
  map = *pmap;

  if (!V8_IS_HEAPOBJECT(map))
    return cd_error(kCDErrNotObject);

  /* Load object type */
  err = cd_v8_get_obj_type(state, node->obj, node->map, &type);
  if (!cd_is_ok(err))
    return err;

  /* Add node to the nodes list as early as possible */
  err = cd_add_node(state, node, type);
  if (!cd_is_ok(err))
    return err;

  /* Enqueue map itself */
  err = cd_queue_ptr(state, node, map, NULL);
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
    off = cd_v8_class_Map__instance_attributes__int + kCDV8MapFieldOffset;
    V8_CORE_PTR(node->obj, off, start);

    /* Constructor + Prototype */
    V8_CORE_PTR(node->obj, off + state->ptr_size * kCDV8MapFieldCount, end);
  } else {
    /* Unknown type - ignore */
    return cd_ok();
  }

  if (start != NULL && end != NULL)
    cd_queue_range(state, node, start, end);

  return cd_ok();
}


cd_error_t cd_queue_ptr(cd_state_t* state,
                        cd_node_t* from,
                        void* ptr,
                        void* map) {
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

    /* Load map, if not provided */
    if (map == NULL) {
      void** pmap;

      V8_CORE_PTR(ptr, cd_v8_class_HeapObject__map__Map, pmap);
      map = *pmap;
    }
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
    node->map = map;
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
  for (; start < end; start += state->ptr_size) {
    cd_error_t err;

    err = cd_queue_ptr(state, from, *(void**) start, NULL);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


cd_error_t cd_add_node(cd_state_t* state, cd_node_t* node, int type) {
  cd_error_t err;
  const char* cname;
  void** ptr;

  /* Mimique V8HeapExplorer::AddEntry */
  if (type == T(JSFunction, JS_FUNCTION)) {
    err = cd_v8_fn_name(state, node->obj, &cname, &node->name);

    node->type = kCDNodeClosure;
  } else if (type == T(JSRegExp, JS_REGEXP)) {
    void* f;
    void* pattern;

    /* Load fixed array */
    V8_CORE_PTR(node->obj,
                cd_v8_class_JSRegExp__data__Object,
                ptr);
    f = *ptr;

    /* Load pattern */
    V8_CORE_PTR(f,
                cd_v8_class_FixedArray__data__uintptr_t +
                    kCDV8RegExpPattern * state->ptr_size,
                ptr);
    pattern = *ptr;

    err = cd_v8_to_cstr(state, pattern, &cname, &node->name);

    node->type = kCDNodeRegExp;
  } else if (type == T(JSObject, JS_OBJECT)) {
    void* cons;
    int ctype;

    V8_CORE_PTR(node->map,
                cd_v8_class_Map__constructor__Object,
                ptr);
    cons = *ptr;

    err = cd_v8_get_obj_type(state, cons, NULL, &ctype);
    if (!cd_is_ok(err))
      return err;

    if (ctype == T(JSFunction, JS_FUNCTION)) {
      err = cd_v8_fn_name(state, cons, &cname, &node->name);
    } else {
      err = cd_strings_copy(&state->strings,
                            &cname,
                            &node->name,
                            "Object",
                            6);
    }

    node->type = kCDNodeObject;
  } else {
    err = cd_strings_copy(&state->strings, &cname, &node->name, "", 0);
    node->type = kCDNodeHidden;
  }
  if (!cd_is_ok(err))
    return err;

  err = cd_v8_get_obj_size(state, node->map, type, &node->size);
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
