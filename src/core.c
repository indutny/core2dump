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

struct cd_state_s {
  cd_obj_t* core;
  cd_obj_t* binary;
  int output;

  cd_list_t roots;
};

static cd_error_t run(const char* input,
                      const char* binary,
                      const char* output);
static cd_error_t cd_obj2json(int input, int binary, int output);
static cd_error_t cd_find_global_cb(void* arg, void* addr, uint64_t size);

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
        break;
      case 'h':
        cd_print_help(argv[0]);
        break;
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

  state.binary = cd_obj_new(binary, &err);
  if (!cd_is_ok(err))
    goto failed_binary_obj;

  state.output = output;

  err = cd_v8_init(state.binary, state.core);
  if (!cd_is_ok(err))
    goto failed_v8_init;

  if (cd_list_init(&state.roots, 4) != 0)
    goto failed_v8_init;

  /* Find Global object instances in memory */
  err = cd_obj_iterate(state.core, cd_find_global_cb, &state);
  if (!cd_is_ok(err) && err.code != kCDErrNotFound)
    goto failed_iterate;

  fprintf(stdout, "found %d global objects\n", state.roots.off);

failed_iterate:
  cd_list_free(&state.roots);

failed_v8_init:
  cd_obj_free(state.binary);

failed_binary_obj:
  cd_obj_free(state.core);

fatal:
  return err;
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


cd_error_t cd_find_global_cb(void* arg, void* addr, uint64_t size) {
  cd_state_t* state;
  uint64_t off;
  uint64_t delta;

  state = arg;
  delta = cd_obj_is_x64(state->core) ? 8 : 4;

  for (off = 0; off < size; off += delta) {
    void* obj;
    void** pmap;
    void* map;
    uint8_t* attrs;

    obj = *(void**)(addr + off);

    /* Find v8 heapobject */
    if (!V8_IS_HEAPOBJECT(obj))
      continue;
    obj = V8_OBJ(obj);

    CORE_PTR(obj + cd_v8_class_HeapObject__map__Map, pmap);
    map = *pmap;

    /* That has a heapobject map */
    if (!V8_IS_HEAPOBJECT(map))
      continue;
    map = V8_OBJ(map);

    CORE_PTR(map + cd_v8_class_Map__instance_attributes__int, attrs);
    if (*attrs != (uint8_t) cd_v8_type_JSGlobalObject__JS_GLOBAL_OBJECT_TYPE)
      continue;

    if (cd_list_push(&state->roots, obj) != 0)
      return cd_error_str(kCDErrNoMem, "cd_list_push roots");
    return cd_ok();
  }

  return cd_error(kCDErrNotFound);
}


#undef V8_IS_HEAPOBJECT
#undef V8_OBJ
#undef CORE_PTR
