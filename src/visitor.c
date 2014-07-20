#include "visitor.h"
#include "common.h"
#include "error.h"
#include "queue.h"
#include "state.h"
#include "v8helpers.h"
#include "v8constants.h"

#include <stdlib.h>
#include <stdio.h>

static cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_queue_range(cd_state_t* state,
                                 cd_node_t* from,
                                 char* start,
                                 char* end);
static cd_error_t cd_add_node(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_node_init(cd_state_t* state,
                               cd_node_t* node,
                               void* ptr,
                               void* map);
static void cd_node_free(cd_state_t* state, cd_node_t* node);


static const int kCDNodesInitialSize = 65536;


cd_error_t cd_visitor_init(cd_state_t* state) {
  cd_error_t err;
  cd_node_t* root;
  const char* ptr;

  QUEUE_INIT(&state->nodes.list);

  state->nodes.count = 0;
  state->nodes.id = 0;
  state->edge_count = 0;

  /* Init root and insert it */
  root = &state->nodes.root;
  QUEUE_INSERT_TAIL(&state->nodes.list, &root->member);
  root->type = kCDNodeSynthetic;
  root->size = 0;
  QUEUE_INIT(&root->edges.incoming);
  QUEUE_INIT(&root->edges.outgoing);
  root->edges.incoming_count = 0;
  root->edges.outgoing_count = 0;
  state->nodes.count++;

  err = cd_strings_copy(&state->strings, &ptr, &root->name, "(GC roots)", 10);
  if (!cd_is_ok(err))
    return err;

  if (cd_hashmap_init(&state->nodes.map, kCDNodesInitialSize) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_init(nodes.map)");

  return cd_ok();
}


void cd_visitor_destroy(cd_state_t* state) {
  QUEUE* qn;

  /* Free edges first */
  QUEUE_FOREACH(qn, &state->nodes.list) {
    cd_node_t* node;

    node = container_of(qn, cd_node_t, member);

    /* Free edges */
    while (!QUEUE_EMPTY(&node->edges.outgoing)) {
      QUEUE* qe;
      cd_edge_t* edge;

      qe = QUEUE_HEAD(&node->edges.outgoing);

      edge = container_of(qe, cd_edge_t, out);
      QUEUE_REMOVE(&edge->in);
      QUEUE_REMOVE(&edge->out);
      free(edge);
    }
  }

  /* Free nodes */
  while (!QUEUE_EMPTY(&state->nodes.list)) {
    cd_node_t* node;

    qn = QUEUE_HEAD(&state->nodes.list);
    QUEUE_REMOVE(qn);
    node = container_of(qn, cd_node_t, member);

    if (node != &state->nodes.root)
      free(node);
  }

  cd_hashmap_destroy(&state->nodes.map);
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  QUEUE* q;
  while (!QUEUE_EMPTY(&state->queue) != 0) {
    cd_node_t* node;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);

    /* Node will be readded to `nodes` in case of success */
    if (!cd_is_ok(cd_visit_root(state, node)))
      cd_node_free(state, node);
  }

  /* Enumerate nodes */
  QUEUE_FOREACH(q, &state->nodes.list) {
    cd_node_t* node;

    node = container_of(q, cd_node_t, member);
    node->id = state->nodes.id++;
  }

  return cd_ok();
}


#define T(A, B) CD_V8_TYPE(A, B)


cd_error_t cd_visit_root(cd_state_t* state, cd_node_t* node) {
  cd_error_t err;
  char* start;
  char* end;
  int type;

  type = node->v8_type;

  if (cd_hashmap_get(&state->nodes.map,
                     (const char*) &node->obj,
                     sizeof(node->obj)) != NULL) {
    return cd_error(kCDErrAlreadyVisited);
  }

  /* Add node to the nodes list as early as possible */
  err = cd_add_node(state, node);
  if (!cd_is_ok(err))
    return err;

  /* Mimique the v8's behaviour, see HeapObject::IterateBody */

  /* Strings... ignore for now */
  if (type < cd_v8_FirstNonstringType)
    return cd_ok();

  start = NULL;
  end = NULL;

  V8_CORE_PTR(node->obj, 0, start);
  V8_CORE_PTR(node->obj, node->size, end);

  if (start != NULL && end != NULL)
    cd_queue_range(state, node, start, end);

  return cd_ok();
}


cd_error_t cd_node_init(cd_state_t* state,
                        cd_node_t* node,
                        void* ptr,
                        void* map) {
  cd_error_t err;

  /* Load map, if not provided */
  if (map == NULL) {
    void** pmap;

    V8_CORE_PTR(ptr, cd_v8_class_HeapObject__map__Map, pmap);
    map = *pmap;

    if (!V8_IS_HEAPOBJECT(map))
      return cd_error(kCDErrNotObject);
  }

  /* Load object type and size */
  err = cd_v8_get_obj_type(state, ptr, map, &node->v8_type);
  if (!cd_is_ok(err))
    return err;

  err = cd_v8_get_obj_size(state, ptr, map, node->v8_type, &node->size);
  if (!cd_is_ok(err))
    return err;

  node->obj = ptr;
  node->map = map;
  state->nodes.count++;

  QUEUE_INIT(&node->member);
  QUEUE_INIT(&node->edges.incoming);
  QUEUE_INIT(&node->edges.outgoing);
  node->edges.incoming_count = 0;
  node->edges.outgoing_count = 0;

  return cd_ok();
}


void cd_node_free(cd_state_t* state, cd_node_t* node) {
  /* Dealloc all incoming edges */
  while (!QUEUE_EMPTY(&node->edges.incoming)) {
    QUEUE* q;
    cd_edge_t* edge;

    q = QUEUE_HEAD(&node->edges.incoming);

    edge = container_of(q, cd_edge_t, in);
    QUEUE_REMOVE(&edge->in);
    QUEUE_REMOVE(&edge->out);

    edge->from->edges.outgoing_count--;
    free(edge);
  }
  QUEUE_INIT(&node->member);
  free(node);

  state->nodes.count--;
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
  } else {
    edge = NULL;
  }

  /* Initialize and queue node if just created */
  if (!existing) {
    cd_error_t err;

    err = cd_node_init(state, node, ptr, map);
    if (!cd_is_ok(err)) {
      free(node);
      free(edge);
      return err;
    }

    QUEUE_INSERT_TAIL(&state->queue, &node->member);
  }

  /* Fill the edge */
  if (edge == NULL)
    return cd_ok();

  edge->from = from;
  edge->to = node;

  /* TODO(indutny) Figure out theese */
  edge->type = kCDEdgeElement;
  edge->name = 0;

  from->edges.outgoing_count++;
  QUEUE_INSERT_TAIL(&from->edges.outgoing, &edge->out);
  node->edges.incoming_count++;
  QUEUE_INSERT_TAIL(&node->edges.incoming, &edge->in);

  state->edge_count++;

  return cd_ok();
}


cd_error_t cd_queue_range(cd_state_t* state,
                          cd_node_t* from,
                          char* start,
                          char* end) {
  for (; start < end; start += state->ptr_size)
    cd_queue_ptr(state, from, *(void**) start, NULL);

  return cd_ok();
}


cd_error_t cd_add_node(cd_state_t* state, cd_node_t* node) {
  cd_error_t err;
  const char* cname;
  void** ptr;
  int type;

  type = node->v8_type;

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
  } else if (type == T(JSObject, JS_OBJECT) ||
             type == T(JSValue, JS_VALUE) ||
             type == T(JSDate, JS_DATE) ||
             type == T(JSGlobalObject, JS_GLOBAL_OBJECT) ||
             type == T(JSBuiltinsObject, JS_BUILTINS_OBJECT) ||
             type == T(JSMessageObject, JS_MESSAGE_OBJECT) ||
             type == T(Map, MAP)) {
    void* cons;
    int ctype;

    V8_CORE_PTR((type == T(Map, MAP) ? node->obj : node->map),
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
  } else if (type < cd_v8_FirstNonstringType) {
    int repr;

    repr = type & cd_v8_StringRepresentationMask;

    if (repr == cd_v8_ConsStringTag) {
      err = cd_strings_copy(&state->strings,
                            &cname,
                            &node->name,
                            "(concatenated string)",
                            21);
      node->type = kCDNodeConString;
    } else if (repr == cd_v8_SlicedStringTag) {
      err = cd_strings_copy(&state->strings,
                            &cname,
                            &node->name,
                            "(sliced string)",
                            15);
      node->type = kCDNodeSlicedString;
    } else {
      err = cd_v8_to_cstr(state, node->obj, &cname, &node->name);
      node->type = kCDNodeString;
    }
  } else if (type == T(SharedFunctionInfo, SHARED_FUNCTION_INFO)) {
    void* name;

    V8_CORE_PTR(node->obj, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
    name = *ptr;

    err = cd_v8_to_cstr(state, name, &cname, &node->name);
    node->type = kCDNodeCode;
  } else if (type == T(HeapNumber, HEAP_NUMBER)) {
    err = cd_strings_copy(&state->strings,
                          &cname,
                          &node->name,
                          "number",
                          6);
    node->type = kCDNodeNumber;
  } else {
    if (type == T(Code, CODE)) {
      node->type = kCDNodeCode;
    } else if (type == T(JSArray, JS_ARRAY) ||
               type == T(JSArrayBuffer, JS_ARRAY_BUFFER) ||
               type == T(JSTypedArray, JS_TYPED_ARRAY) ||
               type == T(FixedArray, FIXED_ARRAY) ||
               type == T(FixedDoubleArray, FIXED_DOUBLE_ARRAY)) {
      node->type = kCDNodeArray;
    } else {
      node->type = kCDNodeHidden;
    }
    err = cd_strings_copy(&state->strings, &cname, &node->name, "", 0);
  }
  if (!cd_is_ok(err))
    return err;

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
