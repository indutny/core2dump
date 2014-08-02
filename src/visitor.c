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

static cd_error_t cd_tag_obj_props(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_tag_obj_fast_props(cd_state_t* state,
                                        cd_node_t* node,
                                        char* props,
                                        int size);
static cd_error_t cd_tag_obj_slow_props(cd_state_t* state,
                                        cd_node_t* node,
                                        char* props,
                                        int size);

static cd_error_t cd_tag_obj_elems(cd_state_t* state, cd_node_t* node);
static cd_error_t cd_tag_obj_fast_elems(cd_state_t* state,
                                        cd_node_t* node,
                                        char* props,
                                        int size);
static cd_error_t cd_tag_obj_slow_elems(cd_state_t* state,
                                        cd_node_t* node,
                                        char* props,
                                        int size);

static cd_error_t cd_tag_map_props(cd_state_t* state, cd_node_t* node);

static cd_error_t cd_tag_fn_props(cd_state_t* state, cd_node_t* node);

static cd_error_t cd_tag_shared_props(cd_state_t* state, cd_node_t* node);

static cd_error_t cd_tag_script_props(cd_state_t* state, cd_node_t* node);

static cd_error_t cd_tag_obj_property(cd_state_t* state,
                                      cd_node_t* node,
                                      void* key,
                                      void* val);
static cd_error_t cd_name(cd_state_t* state,
                          cd_node_t* node,
                          void* ptr,
                          void* map,
                          cd_edge_type_t type,
                          int name,
                          const char* tag,
                          int tag_len);
static cd_error_t cd_tag(cd_state_t* state,
                         cd_node_t* node,
                         void* ptr,
                         void* map,
                         cd_edge_type_t type,
                         const char* tag,
                         int tag_len);


static cd_node_t nil_node;
static const int kCDNodesInitialSize = 65536;
static const int kCDEdgesInitialSize = 65536;


cd_error_t cd_visitor_init(cd_state_t* state) {
  cd_error_t err;
  cd_node_t* root;

  QUEUE_INIT(&state->nodes.list);

  state->nodes.id = 0;
  state->edges.count = 0;

  /* Init root and insert it */
  root = &state->nodes.root;
  QUEUE_INSERT_TAIL(&state->nodes.list, &root->member);
  root->type = kCDNodeSynthetic;
  root->size = 0;
  QUEUE_INIT(&root->edges.incoming);
  QUEUE_INIT(&root->edges.outgoing);
  root->edges.outgoing_count = 0;

  err = cd_strings_copy(&state->strings, NULL, &root->name, "(GC roots)", 10);
  if (!cd_is_ok(err))
    return err;

  if (cd_hashmap_init(&state->nodes.map, kCDNodesInitialSize, 1) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_init(nodes.map)");

  if (cd_hashmap_init(&state->edges.map, kCDEdgesInitialSize, 0) != 0) {
    cd_hashmap_destroy(&state->nodes.map);
    return cd_error_str(kCDErrNoMem, "cd_hashmap_init(edges.map)");
  }

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
      state->edges.count--;
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
  cd_hashmap_destroy(&state->edges.map);
}


cd_error_t cd_visit_roots(cd_state_t* state) {
  QUEUE* q;
  while (!QUEUE_EMPTY(&state->queue) != 0) {
    cd_node_t* node;

    /* Pick first */
    q = QUEUE_NEXT(&state->queue);
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

  /* Tag map */
  cd_tag(state, node, node->map, NULL, kCDEdgeInternal, "(map)", 5);

  /* Tag properties */
  cd_tag_obj_props(state, node);

  /* Tag elements */
  cd_tag_obj_elems(state, node);

  /* Tag map properties */
  cd_tag_map_props(state, node);

  /* Tag function properties */
  cd_tag_fn_props(state, node);

  /* Tag function shared info properties */
  cd_tag_shared_props(state, node);

  /* Tag function script info properties */
  cd_tag_script_props(state, node);

  /* Queue all pointers */
  if (start != NULL && end != NULL)
    cd_queue_range(state, node, start, end);

  return cd_ok();
}


cd_error_t cd_tag_obj_props(cd_state_t* state, cd_node_t* node) {
  cd_error_t err;
  int type;
  int fast;
  void** ptr;
  void* props;
  int size;

  type = node->v8_type;
  if (type != T(JSObject, JS_OBJECT) &&
      type != T(JSArray, JS_ARRAY) &&
      type != T(JSArrayBuffer, JS_ARRAY_BUFFER) &&
      type != T(JSValue, JS_VALUE) &&
      type != T(JSDate, JS_DATE) &&
      type != T(JSGlobalObject, JS_GLOBAL_OBJECT) &&
      type != T(JSMessageObject, JS_MESSAGE_OBJECT) &&
      type != T(JSFunction, JS_FUNCTION)) {
    return cd_ok();
  }

  /* Tag prototype */
  V8_CORE_PTR(node->map, cd_v8_class_Map__prototype__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeProperty, "(prototype)", 11);

  /* Tag constructor */
  V8_CORE_PTR(node->map, cd_v8_class_Map__constructor__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeProperty, "(constructor)", 13);

  /* Tag fast or slow properties */
  V8_CORE_PTR(node->obj, cd_v8_class_JSObject__properties__FixedArray, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeHidden, "(properties)", 12);
  cd_name(state, node, *ptr, NULL, kCDEdgeHidden, 0, "(properties)", 12);
  props = *(char**) ptr;

  err = cd_v8_obj_has_fast_props(state, node->obj, node->map, &fast);
  if (!cd_is_ok(err))
    return err;

  err = cd_v8_get_fixed_arr_data(state, props, &props, &size);
  if (!cd_is_ok(err))
    return err;

  if (fast)
    return cd_tag_obj_fast_props(state, node, props, size);
  else
    return cd_tag_obj_slow_props(state, node, props, size);
}


cd_error_t cd_tag_obj_fast_props(cd_state_t* state,
                                 cd_node_t* node,
                                 char* props,
                                 int size) {
  cd_error_t err;
  void** ptr;
  void* desc_array;
  void* desc_data;
  int desc_size;
  int off;
  int inobj;

  V8_CORE_PTR(node->map,
              cd_v8_class_Map__instance_descriptors__DescriptorArray,
              ptr);
  desc_array = *ptr;

  err = cd_v8_get_fixed_arr_data(state, desc_array, &desc_data, &desc_size);
  if (!cd_is_ok(err))
    return err;

  off = cd_v8_prop_idx_first;
  if ((desc_size - off) % cd_v8_prop_desc_size != 0)
    return cd_error(kCDErrNotSoSlow);

  V8_CORE_PTR(node->map, cd_v8_class_Map__inobject_properties__int, ptr);
  inobj = *(int8_t*) ptr;

  for (; off < desc_size; off += cd_v8_prop_desc_size) {
    char* i;
    void* key;
    void* val;
    int det;
    int idx;
    int type;

    i = (char*) desc_data + off * state->ptr_size;
    det = V8_SMI(*(void**)(i + cd_v8_prop_desc_details * state->ptr_size));

    type = det & cd_v8_prop_type_mask;
    idx = (det & cd_v8_prop_index_mask) >> cd_v8_prop_index_shift;

    if (type != cd_v8_prop_type_field)
      continue;

    key = *(void**)(i + cd_v8_prop_desc_key * state->ptr_size);
    if (idx < inobj) {
      int inobj_off;

      inobj_off = node->size + (idx - inobj) * state->ptr_size;
      V8_CORE_PTR(node->obj, inobj_off, ptr)
      val = *ptr;
    } else {
      val = *(void**)(i + cd_v8_prop_desc_value * state->ptr_size);
    }

    cd_tag_obj_property(state, node, key, val);
  }

  return cd_ok();
}


cd_error_t cd_tag_obj_slow_props(cd_state_t* state,
                                 cd_node_t* node,
                                 char* props,
                                 int size) {
  int off;
  int prefix;
  int entry;

  prefix = cd_v8_class_NameDictionaryShape__prefix_size__int;
  entry = cd_v8_class_NameDictionaryShape__prefix_size__int;

  if ((size - prefix) % entry != 0)
    return cd_error(kCDErrNotSoFast);

  /* Queue each property */
  for (off = prefix; off < size; off += entry) {
    void* key;
    void* val;

    key = *(char**) (props + off * state->ptr_size);
    val = *(char**) (props + (off + 1) * state->ptr_size);

    cd_tag_obj_property(state, node, key, val);
  }

  return cd_ok();
}


cd_error_t cd_tag_map_props(cd_state_t* state, cd_node_t* node) {
  void** ptr;

  if (node->v8_type != T(Map, MAP))
    return cd_ok();

  V8_CORE_PTR(node->obj,
              cd_v8_class_Map__instance_descriptors__DescriptorArray,
              ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(map descriptors)", 17);

  V8_CORE_PTR(node->obj, cd_v8_class_Map__code_cache__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(code cache)", 12);
  V8_CORE_PTR(node->obj, cd_v8_class_Map__constructor__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(constructor)", 13);
  V8_CORE_PTR(node->obj, cd_v8_class_Map__prototype__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(prototype)", 11);
  V8_CORE_PTR(node->obj, cd_v8_class_Map__dependent_code__DependentCode, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(dependent code)", 16);

  return cd_ok();
}


cd_error_t cd_tag_fn_props(cd_state_t* state, cd_node_t* node) {
  void** ptr;

  if (node->v8_type != T(JSFunction, JS_FUNCTION))
    return cd_ok();

  /* Load shared function info to lookup name */
  V8_CORE_PTR(node->obj,
              cd_v8_class_JSFunction__shared__SharedFunctionInfo,
              ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "(shared)", 8);

  return cd_ok();
}


cd_error_t cd_tag_shared_props(cd_state_t* state, cd_node_t* node) {
  void** ptr;

  if (node->v8_type != T(SharedFunctionInfo, SHARED_FUNCTION_INFO))
    return cd_ok();

  V8_CORE_PTR(node->obj, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "name", 4);
  V8_CORE_PTR(node->obj,
              cd_v8_class_SharedFunctionInfo__inferred_name__String,
              ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "inferred name", 13);
  V8_CORE_PTR(node->obj, cd_v8_class_SharedFunctionInfo__script__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "script", 6);

  return cd_ok();
}


cd_error_t cd_tag_script_props(cd_state_t* state, cd_node_t* node) {
  void** ptr;

  if (node->v8_type != T(Script, SCRIPT))
    return cd_ok();

  V8_CORE_PTR(node->obj, cd_v8_class_Script__source__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "source", 6);
  V8_CORE_PTR(node->obj, cd_v8_class_Script__name__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "name", 4);
  V8_CORE_PTR(node->obj, cd_v8_class_Script__context_data__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeInternal, "context data", 12);

  return cd_ok();
}


cd_error_t cd_tag_obj_elems(cd_state_t* state, cd_node_t* node) {
  cd_error_t err;
  int type;
  int fast;
  void** ptr;
  void* elems;
  int size;

  type = node->v8_type;
  if (type != T(JSObject, JS_OBJECT) &&
      type != T(JSArray, JS_ARRAY) &&
      type != T(JSArrayBuffer, JS_ARRAY_BUFFER) &&
      type != T(JSValue, JS_VALUE) &&
      type != T(JSDate, JS_DATE) &&
      type != T(JSGlobalObject, JS_GLOBAL_OBJECT) &&
      type != T(JSMessageObject, JS_MESSAGE_OBJECT) &&
      type != T(JSFunction, JS_FUNCTION)) {
    return cd_ok();
  }

  /* Tag fast or slow properties */
  V8_CORE_PTR(node->obj, cd_v8_class_JSObject__elements__Object, ptr);
  cd_tag(state, node, *ptr, NULL, kCDEdgeHidden, "(elements)", 10);
  elems = *(char**) ptr;

  err = cd_v8_obj_has_fast_elems(state, node->obj, node->map, &fast);
  if (!cd_is_ok(err))
    return err;

  err = cd_v8_get_fixed_arr_data(state, elems, &elems, &size);
  if (!cd_is_ok(err))
    return err;

  if (fast)
    return cd_tag_obj_fast_elems(state, node, elems, size);
  else
    return cd_tag_obj_slow_elems(state, node, elems, size);
}


cd_error_t cd_tag_obj_fast_elems(cd_state_t* state,
                                 cd_node_t* node,
                                 char* props,
                                 int size) {
  int off;
  for (off = 0; off < size; off++) {
    cd_error_t err;
    void* val;
    int is_hole;

    val = *(void**) (props + off * state->ptr_size);
    err = cd_v8_is_hole(state, val, &is_hole);
    if (!cd_is_ok(err) || is_hole)
      continue;

    cd_tag_obj_property(state, node, V8_TAG_SMI(off), val);
  }
  return cd_ok();
}


cd_error_t cd_tag_obj_slow_elems(cd_state_t* state,
                                 cd_node_t* node,
                                 char* props,
                                 int size) {
  int off;
  int delta;
  int entry;

  entry = cd_v8_class_NumberDictionaryShape__entry_size__int;
  delta = size % entry;
  if (delta != 0) {
    size -= delta;
    props += delta * state->ptr_size;
  }

  /* Queue each property */
  for (off = 0; off < size; off += entry) {
    char* ptr;
    void* key;
    void* val;

    ptr = props + off * state->ptr_size;

    key = *(char**) (ptr);
    val = *(char**) (ptr + state->ptr_size);

    cd_tag_obj_property(state, node, key, val);
  }
  return cd_ok();
}


cd_error_t cd_tag_obj_property(cd_state_t* state,
                               cd_node_t* node,
                               void* key,
                               void* val) {
  cd_error_t err;
  int key_type;
  int key_name;

  if (V8_IS_SMI(key)) {
    return cd_queue_ptr(state,
                        node,
                        val,
                        NULL,
                        kCDEdgeElement,
                        V8_SMI(key),
                        1,
                        NULL);
  }

  err = cd_v8_get_obj_type(state, key, NULL, &key_type);
  if (!cd_is_ok(err))
    return err;

  /* Skip non-string object keys */
  if (key_type >= cd_v8_FirstNonstringType)
    return cd_ok();

  err = cd_v8_to_cstr(state, key, NULL, NULL, &key_name);
  if (!cd_is_ok(err))
    return err;

  return cd_queue_ptr(state,
                      node,
                      val,
                      NULL,
                      kCDEdgeProperty,
                      key_name,
                      1,
                      NULL);
}


cd_error_t cd_name(cd_state_t* state,
                   cd_node_t* node,
                   void* ptr,
                   void* map,
                   cd_edge_type_t type,
                   int name,
                   const char* tag,
                   int tag_len) {
  cd_error_t err;
  cd_node_t* to;

  err = cd_queue_ptr(state, node, ptr, map, type, name, 1, &to);
  if (!cd_is_ok(err))
    return err;

  return cd_strings_copy(&state->strings, NULL, &to->name, tag, tag_len);
}


cd_error_t cd_tag(cd_state_t* state,
                  cd_node_t* node,
                  void* ptr,
                  void* map,
                  cd_edge_type_t type,
                  const char* tag,
                  int tag_len) {
  cd_error_t err;
  int name;

  err = cd_strings_copy(&state->strings, NULL, &name, tag, tag_len);
  if (!cd_is_ok(err))
    return err;

  return cd_queue_ptr(state, node, ptr, map, type, name, 1, NULL);
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
  node->name = 0;

  QUEUE_INIT(&node->member);
  QUEUE_INIT(&node->edges.incoming);
  QUEUE_INIT(&node->edges.outgoing);
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

    edge->key.from->edges.outgoing_count--;
    cd_hashmap_delete(&state->edges.map,
                      (const char*) &edge->key,
                      sizeof(edge->key));
    free(edge);
  }
  QUEUE_REMOVE(&node->member);

  cd_hashmap_insert(&state->nodes.map,
                    (const char*) node->obj,
                    sizeof(node->obj),
                    &nil_node);
  free(node);
}


cd_error_t cd_queue_ptr(cd_state_t* state,
                        cd_node_t* from,
                        void* ptr,
                        void* map,
                        cd_edge_type_t type,
                        int name,
                        int tag,
                        cd_node_t** out) {
  cd_error_t err;
  cd_node_t* node;
  cd_edge_t* edge;
  cd_edge_t* old_edge;
  int existing;

  if (!V8_IS_HEAPOBJECT(ptr))
    return cd_error(kCDErrNotObject);

  node = cd_hashmap_get(&state->nodes.map, (const char*) ptr, sizeof(ptr));
  if (node == &nil_node)
    return cd_ok();

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
      err = cd_error_str(kCDErrNoMem, "cd_edge_t");
      goto fatal;
    }
  } else {
    edge = NULL;
  }

  /* Initialize and queue node if just created */
  if (!existing) {
    err = cd_node_init(state, node, ptr, map);
    if (!cd_is_ok(err))
      goto fatal;

    QUEUE_INSERT_TAIL(&state->queue, &node->member);
  }

  /* Fill the edge */
  if (edge == NULL)
    goto done;

  edge->key.from = from;
  edge->key.to = node;

  edge->type = type;
  edge->name = name;

  /* Existing edge found */
  old_edge = cd_hashmap_get(&state->edges.map,
                            (const char*) &edge->key,
                            sizeof(edge->key));
  if (old_edge != NULL) {
    if (tag) {
      old_edge->type = type;
      old_edge->name = name;
    }
    free(edge);
    goto done;
  }

  if (cd_hashmap_insert(&state->edges.map,
                        (const char*) &edge->key,
                        sizeof(edge->key),
                        edge) != 0) {
    err = cd_error_str(kCDErrNoMem, "cd_edge_t hashmap insert");
    goto fatal;
  }

  from->edges.outgoing_count++;

  QUEUE_INSERT_TAIL(&from->edges.outgoing, &edge->out);
  QUEUE_INSERT_TAIL(&node->edges.incoming, &edge->in);

  state->edges.count++;

done:
  if (cd_hashmap_insert(&state->nodes.map,
                        (const char*) node->obj,
                        sizeof(node->obj),
                        node) != 0) {
    err = cd_error_str(kCDErrNoMem, "cd_hashmap_insert(nodes.map)");
    goto fatal;
  }

  if (out != NULL)
    *out = node;

  return cd_ok();

fatal:
  if (!existing)
    free(node);
  free(edge);

  return err;
}


cd_error_t cd_queue_range(cd_state_t* state,
                          cd_node_t* from,
                          char* start,
                          char* end) {
  const char* cur;
  int i;

  for (i = 0, cur = start; cur < end; cur += state->ptr_size, i++)
    cd_queue_ptr(state,from, *(void**) cur, NULL, kCDEdgeElement, i, 0, NULL);

  return cd_ok();
}


cd_error_t cd_add_node(cd_state_t* state, cd_node_t* node) {
  cd_error_t err;
  void** ptr;
  int type;
  int name;

  type = node->v8_type;

  /* Mimique V8HeapExplorer::AddEntry */
  if (type == T(JSFunction, JS_FUNCTION)) {
    err = cd_v8_fn_info(state, node->obj, NULL, NULL, &name, NULL);

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

    err = cd_v8_to_cstr(state, pattern, NULL, NULL, &name);

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
      err = cd_v8_fn_info(state, cons, NULL, NULL, &name, NULL);
    } else {
      err = cd_strings_copy(&state->strings,
                            NULL,
                            &name,
                            "Object",
                            6);
    }

    node->type = kCDNodeObject;
  } else if (type < cd_v8_FirstNonstringType) {
    int repr;

    repr = type & cd_v8_StringRepresentationMask;

    if (repr == cd_v8_ConsStringTag) {
      err = cd_strings_copy(&state->strings,
                            NULL,
                            &name,
                            "(concatenated string)",
                            21);
      node->type = kCDNodeConString;
    } else if (repr == cd_v8_SlicedStringTag) {
      err = cd_strings_copy(&state->strings,
                            NULL,
                            &name,
                            "(sliced string)",
                            15);
      node->type = kCDNodeSlicedString;
    } else {
      err = cd_v8_to_cstr(state, node->obj, NULL, NULL, &name);
      node->type = kCDNodeString;
    }
  } else if (type == T(SharedFunctionInfo, SHARED_FUNCTION_INFO)) {
    void* sname;

    V8_CORE_PTR(node->obj, cd_v8_class_SharedFunctionInfo__name__Object, ptr);
    sname = *ptr;

    err = cd_v8_to_cstr(state, sname, NULL, NULL, &name);
    node->type = kCDNodeCode;
  } else if (type == T(Script, SCRIPT)) {
    void* sname;

    V8_CORE_PTR(node->obj, cd_v8_class_Script__name__Object, ptr);
    sname = *ptr;
    err = cd_v8_to_cstr(state, sname, NULL, NULL, &name);
    node->type = kCDNodeCode;
  } else if (type == T(HeapNumber, HEAP_NUMBER)) {
    err = cd_strings_copy(&state->strings,
                          NULL,
                          &name,
                          "number",
                          6);
    node->type = kCDNodeNumber;
  } else if (type == T(Oddball, ODDBALL)) {
    err = cd_strings_copy(&state->strings,
                          NULL,
                          &name,
                          "oddball",
                          7);
    node->type = kCDNodeHidden;
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
    err = cd_strings_copy(&state->strings, NULL, &name, "", 0);
  }
  if (!cd_is_ok(err))
    return err;

  if (node->name == 0)
    node->name = name;

  QUEUE_INSERT_TAIL(&state->nodes.list, &node->member);

  return cd_ok();
}


#undef T
