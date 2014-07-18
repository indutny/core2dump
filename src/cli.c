#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "error.h"
#include "collector.h"
#include "common.h"
#include "obj.h"
#include "strings.h"
#include "version.h"
#include "visitor.h"
#include "v8constants.h"
#include "v8helpers.h"

static cd_error_t run(const char* input,
                      const char* binary,
                      const char* output);
static cd_error_t cd_obj2json(int input, int binary, int output);
static cd_error_t cd_print_dump(cd_state_t* state);


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

  state.ptr_size = cd_obj_is_x64(state.core) ? 8 : 4;

  state.zap_bit = cd_obj_is_x64(state.core) ? 0x7000000000000000LL : 0x70000000;

  state.binary = cd_obj_new(binary, &err);
  if (!cd_is_ok(err))
    goto failed_binary_obj;

  state.output = output;

  err = cd_strings_init(&state.strings);
  if (!cd_is_ok(err))
    goto failed_cd_strings_init;

  err = cd_v8_init(state.binary, state.core);
  if (!cd_is_ok(err))
    goto failed_v8_init;

  err = cd_visitor_init(&state);
  if (!cd_is_ok(err))
    goto failed_v8_init;

  err = cd_collect_roots(&state);
  if (!cd_is_ok(err))
    goto failed_collect_roots;

  err = cd_visit_roots(&state);
  if (!cd_is_ok(err))
    goto failed_collect_roots;

  err = cd_print_dump(&state);
  if (!cd_is_ok(err))
    goto failed_collect_roots;

failed_collect_roots:
  cd_visitor_destroy(&state);

failed_v8_init:
  cd_strings_destroy(&state.strings);

failed_cd_strings_init:
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
      "  \"trace_tree\": [],\n",
      42,
      cd_list_len(&state->nodes),
      0,
      0);

  /* Print all accumulated strings */
  dprintf(state->output, "  \"strings\": [ ");
  cd_strings_print(&state->strings, state->output);
  dprintf(state->output, " ]\n}");

  return cd_ok();
}
