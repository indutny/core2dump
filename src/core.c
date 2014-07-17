#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "error.h"
#include "common.h"
#include "obj.h"
#include "version.h"
#include "v8constants.h"

typedef struct cd_state_s cd_state_t;
typedef cd_error_t (*cd_visit_cb)(cd_state_t* state, void* obj);

struct cd_state_s {
  cd_obj_t* core;
  cd_obj_t* binary;
  cd_obj_thread_t thread;
  int output;

  cd_list_t queue;
  cd_list_t nodes;
  intptr_t zap_bit;
};

static cd_error_t run(const char* input,
                      const char* binary,
                      const char* output);
static cd_error_t cd_obj2json(int input, int binary, int output);
static cd_error_t cd_print_dump(cd_state_t* state);
static cd_error_t cd_collect_roots(cd_state_t* state);
static cd_error_t cd_collect_root(cd_state_t* state, void* ptr);
static cd_error_t cd_visit_roots(cd_state_t* state, cd_visit_cb cb);
static cd_error_t cd_visit_root(cd_state_t* state, void* obj, cd_visit_cb cb);
static cd_error_t cd_print_obj(cd_state_t* state, void* obj);
static cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                                     void* map,
                                     int type,
                                     int* size);
static cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end);


void cd_print_version() {
  fprintf(stderr,
          "core2dump v%d.%d.%d\n",
          CD_VERSION_MAJOR,
          CD_VERSION_MINOR,
          CD_VERSION_PATCH);
}


void cd_print_help(const char* name) {
  fprintf(stderr,
          "Usage: %s [options]\n\n"
              "options:\n"
              " --version, -v           Print version\n"
              " --help, -h              Print this message\n"
              " --input PATH, -i PATH   Specify input   (Required)\n"
              " --binary PATH, -b PATH   Specify binary (Required)\n"
              " --output PATH, -o PATH  Specify output  (Default: stdout)\n",
          name);
}


int main(int argc, char** argv) {
  struct option long_options[] = {
    { "version", 0, NULL, 'v' },
    { "help", 1, NULL, 'h' },
    { "input", 2, NULL, 'i' },
    { "output", 3, NULL, 'o' },
    { "binary", 4, NULL, 'b' },
  };
  int c;
  const char* input;
  const char* output;
  const char* binary;
  cd_error_t err;

  input = NULL;
  output = NULL;
  binary = NULL;

  do {
    c = getopt_long(argc, argv, "hvi:b:o:", long_options, NULL);
    switch (c) {
      case 'v':
        cd_print_version();
        return 0;
      case 'h':
        cd_print_help(argv[0]);
        return 0;
      case 'i':
        input = optarg;
        break;
      case 'o':
        output = optarg;
        break;
      case 'b':
        binary = optarg;
        break;
      default:
        c = -1;
        break;
    }
  } while (c != -1);

  if (input == NULL) {
    cd_print_help(argv[0]);
    fprintf(stderr, "\nInput is a required argument\n");
    return 1;
  }

  if (binary == NULL) {
    cd_print_help(argv[0]);
    fprintf(stderr, "\nBinary is a required argument\n");
    return 1;
  }

  if (output == NULL)
    output = "/dev/stdout";

  err = run(input, binary, output);
  if (!cd_is_ok(err)) {
    fprintf(stderr, "Failed with error code: %d\n", err.code);
    return 1;
  }

  return 0;
}


/* Open files and execute obj2json */
cd_error_t run(const char* input, const char* binary, const char* output) {
  cd_error_t err;
  struct {
    int input;
    int output;
    int binary;
  } fds;

  fds.input = open(input, O_RDONLY);
  if (fds.input == -1) {
    err = cd_error_num(kCDErrInputNotFound, errno);
    goto failed_open_input;
  }

  fds.binary = open(binary, O_RDONLY);
  if (fds.binary == -1) {
    err = cd_error_num(kCDErrBinaryNotFound, errno);
    goto failed_open_binary;
  }

  fds.output = open(output, O_WRONLY);
  if (fds.output == -1) {
    err = cd_error_num(kCDErrOutputNotFound, errno);
    goto failed_open_output;
  }

  err = cd_obj2json(fds.input, fds.binary, fds.output);

  /* Clean-up */
  close(fds.output);

failed_open_output:
  close(fds.binary);

failed_open_binary:
  close(fds.input);

failed_open_input:
  return err;
}


cd_error_t cd_obj2json(int input, int binary, int output) {
  cd_error_t err;
  cd_state_t state;

  state.core = cd_obj_new(input, &err);
  if (!cd_is_ok(err))
    goto fatal;

  state.zap_bit = cd_obj_is_x64(state.core) ? 0x7000000000000000LL : 0x70000000;

  state.binary = cd_obj_new(binary, &err);
  if (!cd_is_ok(err))
    goto failed_binary_obj;

  state.output = output;

  err = cd_v8_init(state.binary, state.core);
  if (!cd_is_ok(err))
    goto failed_v8_init;

  if (cd_list_init(&state.queue, 32) != 0)
    goto failed_v8_init;

  if (cd_list_init(&state.nodes, 32) != 0)
    goto failed_nodes_init;

  err = cd_obj_get_thread(state.core, 0, &state.thread);
  if (!cd_is_ok(err))
    goto failed_get_thread;

  err = cd_collect_roots(&state);
  if (!cd_is_ok(err))
    goto failed_get_thread;

  err = cd_visit_roots(&state, cd_print_obj);
  if (!cd_is_ok(err))
    goto failed_get_thread;

  err = cd_print_dump(&state);
  if (!cd_is_ok(err))
    goto failed_get_thread;

failed_get_thread:
  cd_list_free(&state.nodes);

failed_nodes_init:
  cd_list_free(&state.queue);

failed_v8_init:
  cd_obj_free(state.binary);

failed_binary_obj:
  cd_obj_free(state.core);

fatal:
  return err;
}


cd_error_t cd_print_dump(cd_state_t* state) {
  /* XXX Could be in a separate file */
  dprintf(
      state->output,
      "{\n"
      "  \"snapshot\": {\n"
      "    \"title\": \"heapdump by core2dump\",\n"
      "    \"uid\": %d,\n"
      "    \"meta\": {\n"
      "      \"node_fields\": [\n"
      "        \"type\", \"name\", \"id\", \"self_size\", \"edge_count\",\n"
      "        \"trace_node_id\"\n"
      "      ],\n"
      "      \"node_types\": [\n"
      "        [ \"hidden\", \"array\", \"string\", \"object\", \"code\",\n"
      "          \"closure\", \"regexp\", \"number\", \"native\",\n"
      "          \"synthetic\", \"concatenated string\", \"sliced string\" ],\n"
      "        \"string\", \"number\", \"number\", \"number\", \"number\",\n"
      "        \"number\"\n"
      "      ],\n"
      "      \"edge_fields\": [ \"type\", \"name_or_index\", \"to_node\" ],\n"
      "      \"edge_types\": [\n"
      "        [ \"context\", \"element\", \"property\", \"internal\",\n"
      "          \"hidden\", \"shortcut\", \"weak\" ],\n"
      "        \"string_or_number\", \"node\"\n"
      "      ],\n"
      "      \"trace_function_info_fields\": [\n"
      "        \"function_id\", \"name\", \"script_name\", \"script_id\",\n"
      "        \"line\", \"column\"\n"
      "      ],\n"
      "      \"trace_node_fields\": [\n"
      "        \"id\", \"function_info_index\", \"count\", \"size\",\n"
      "        \"children\"\n"
      "      ]\n"
      "    },\n"
      "    \"node_count\": %d,\n"
      "    \"edge_count\": %d,\n"
      "    \"trace_function_count\": %d\n"
      "  },\n"
      "  \"nodes\": [],\n"
      "  \"edges\": [],\n"
      "  \"trace_function_infos\": [],\n"
      "  \"trace_tree\": [],\n"
      "  \"strings\": []\n"
      "}\n",
      42,
      1,
      2,
      3);

  return cd_ok();
}


#define V8_IS_HEAPOBJECT(ptr)                                                 \
    ((((intptr_t) ptr) & cd_v8_HeapObjectTagMask) == cd_v8_HeapObjectTag)

#define V8_OBJ(ptr) ((void*) ((char*) ptr - cd_v8_HeapObjectTag))

#define CORE_PTR(ptr, out)                                                    \
    do {                                                                      \
      cd_error_t err;                                                         \
      err = cd_obj_get(state->core,                                           \
                       (uint64_t) (ptr),                                      \
                       sizeof(*(out)),                                        \
                       (void**) &(out));                                      \
      if (!cd_is_ok(err))                                                     \
        return err;                                                           \
    } while (0);                                                              \

cd_error_t cd_collect_roots(cd_state_t* state) {
  cd_error_t err;
  uint64_t off;
  uint64_t delta;
  void* stack;
  size_t stack_size;
  unsigned int i;

  /* Visit stack */
  stack_size = state->thread.stack.bottom - state->thread.stack.top;
  err = cd_obj_get(state->core,
                   state->thread.stack.top,
                   stack_size,
                   &stack);
  if (!cd_is_ok(err))
    return err;

  delta = cd_obj_is_x64(state->core) ? 8 : 4;
  for (off = 0; off < stack_size; off += delta)
    cd_collect_root(state, *(void**)((char*) stack + off));

  /* Visit registers */
  for (i = 0; i < state->thread.regs.count; i++)
    cd_collect_root(state, (void*) (intptr_t) state->thread.regs.values[i]);

  return cd_ok();
}


cd_error_t cd_collect_root(cd_state_t* state, void* ptr) {
  void* obj;
  void** pmap;
  void* map;
  uint8_t* attrs;

  obj = ptr;

  /* Find v8 heapobject */
  if (!V8_IS_HEAPOBJECT(obj))
    return cd_error(kCDErrNotObject);
  obj = V8_OBJ(obj);

  CORE_PTR(obj + cd_v8_class_HeapObject__map__Map, pmap);
  map = *pmap;

  /* That has a heapobject map */
  if (!V8_IS_HEAPOBJECT(map))
    return cd_error(kCDErrNotObject);
  map = V8_OBJ(map);

  /* Just to verify that the object has live map */
  CORE_PTR(map + cd_v8_class_Map__instance_attributes__int, attrs);

  if (cd_list_push(&state->queue, obj) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push queue");

  return cd_ok();
}


cd_error_t cd_visit_roots(cd_state_t* state, cd_visit_cb cb) {
  while (cd_list_len(&state->queue) != 0)
    cd_visit_root(state, cd_list_shift(&state->queue), cb);

  return cd_ok();
}


#define T(M, S) cd_v8_type_##M##__##S##_TYPE


cd_error_t cd_visit_root(cd_state_t* state, void* obj, cd_visit_cb cb) {
  void** pmap;
  void* map;
  uint8_t* ptype;
  int type;
  cd_error_t err;

  CORE_PTR(obj + cd_v8_class_HeapObject__map__Map, pmap);

  /* If zapped - the node was already added */
  map = *pmap;
  if (((intptr_t) map & state->zap_bit) == state->zap_bit)
    return cd_ok();
  *pmap = (void*) ((intptr_t) map | state->zap_bit);

  /* That has a heapobject map */
  if (!V8_IS_HEAPOBJECT(map))
    return cd_error(kCDErrNotObject);
  map = V8_OBJ(map);

  /* Load object type */
  CORE_PTR(map + cd_v8_class_Map__instance_attributes__int, ptype);
  type = *ptype;

  /* Push node */
  if (cd_list_push(&state->nodes, obj) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push nodes");

  /* Mimique the v8's behaviour, see HeapObject::IterateBody */

  if (type < cd_v8_FirstNonstringType) {
    /* Strings... ignore for now */
    return cd_ok();
  }

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
      type == T(JSMessageObject, JS_MESSAGE_OBJECT)) {
    /* General object */
    char* start;
    char* end;
    int size;

    err = cd_v8_get_obj_size(state, map, type, &size);
    if (!cd_is_ok(err))
      return err;

    CORE_PTR(obj + cd_v8_class_JSObject__properties__FixedArray, start);
    CORE_PTR(obj +
                cd_v8_class_JSObject__properties__FixedArray +
                size,
             end);

    err = cd_queue_space(state, start, end);
  } else {
  }
  err = cd_ok();

  if (!cd_is_ok(err))
    return err;

  return cd_ok();
}


cd_error_t cd_v8_get_obj_size(cd_state_t* state,
                              void* map,
                              int type,
                              int* size) {
  int instance_size;
  uint8_t* ptr;

  CORE_PTR(map + cd_v8_class_Map__instance_size__int, ptr);
  instance_size = *ptr;

  /* Constant size */
  if (instance_size != 0) {
    *size = instance_size * (cd_obj_is_x64(state->core) ? 8 : 4);
    return cd_ok();
  }

  /* Variable-size */

  *size = 0;
  return cd_ok();
}


cd_error_t cd_queue_space(cd_state_t* state, char* start, char* end) {
  size_t delta;

  delta = cd_obj_is_x64(state->core) ? 8 : 4;
  for (; start < end; start += delta) {
    void* ptr;

    ptr = *(void**) start;
    if (!V8_IS_HEAPOBJECT(ptr))
      return cd_error(kCDErrNotObject);
    ptr = V8_OBJ(ptr);

    if (cd_list_push(&state->queue, ptr) != 0)
      return cd_error_str(kCDErrNoMem, "cd_list_push queue space");
  }

  return cd_ok();
}


#undef T


cd_error_t cd_print_obj(cd_state_t* state, void* obj) {
  return cd_ok();
}


#undef V8_IS_HEAPOBJECT
#undef V8_OBJ
#undef CORE_PTR
