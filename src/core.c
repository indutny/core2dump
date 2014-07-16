#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <unistd.h>

#include "error.h"
#include "obj.h"
#include "version.h"

static cd_error_t run(const char* input,
                      const char* binary,
                      const char* output);
static cd_error_t obj2json(int input, int binary, int output);

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

  err = obj2json(fds.input, fds.binary, fds.output);

  /* Clean-up */
  close(fds.output);

failed_open_output:
  close(fds.binary);

failed_open_binary:
  close(fds.input);

failed_open_input:
  return err;
}


cd_error_t obj2json(int input, int binary, int output) {
  cd_error_t err;
  cd_obj_t* obj;
  cd_obj_t* bobj;
  void* addr;
  uint64_t addr_off;

  obj = cd_obj_new(input, &err);
  if (!cd_is_ok(err))
    goto fatal;

  bobj = cd_obj_new(binary, &err);
  if (!cd_is_ok(err))
    goto failed_binary_obj;

  err = cd_obj_get(obj, 0x7fff5fc00000LL - 8, 8, &addr);
  if (!cd_is_ok(err))
    goto failed_obj_get;

  err = cd_obj_get_sym(bobj, "_main", &addr_off);
  if (!cd_is_ok(err))
    goto failed_obj_get;

  fprintf(stdout, "%llx\n", addr_off);

failed_obj_get:
  cd_obj_free(bobj);

failed_binary_obj:
  cd_obj_free(obj);

fatal:
  return err;
}
