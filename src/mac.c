#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mach-o/loader.h>

#include "error.h"
#include "obj.h"


struct cd_obj_s {
  int fd;
  int is_x64;
  union {
    struct mach_header ia32;
    struct mach_header_64 x64;
  } hdr;
};


static cd_error_t cd_obj_read_header(cd_obj_t* obj);
static cd_error_t cd_obj_read_cmd(cd_obj_t* obj,
                                  struct load_command* cmd,
                                  size_t size,
                                  size_t* res);
static cd_error_t cd_obj_read_seg(cd_obj_t* obj, struct segment_command* cmd);
static cd_error_t cd_obj_read_seg_64(cd_obj_t* obj,
                                     struct segment_command_64* cmd);


cd_obj_t* cd_obj_new(int fd, cd_error_t* err) {
  cd_obj_t* obj;
  uint32_t magic;

  obj = malloc(sizeof(*obj));
  if (obj == NULL) {
    *err = cd_error_str(kCDErrNoMem, "cd_obj_t");
    goto failed_malloc;
  }
  obj->fd = fd;

  *err = cd_pread(fd, &magic, sizeof(magic), 0, NULL);
  if (!cd_is_ok(*err))
    goto failed_pread;

  /* Big-Endian not supported */
  if (magic == MH_CIGAM || magic == MH_CIGAM_64) {
    *err = cd_error_num(kCDErrBigEndianMagic, magic);
    goto failed_pread;
  }

  if (magic != MH_MAGIC && magic != MH_MAGIC_64) {
    *err = cd_error_num(kCDErrInvalidMagic, magic);
    goto failed_pread;
  }
  obj->is_x64 = magic == MH_MAGIC_64;

  /* Time to read header */
  *err = cd_obj_read_header(obj);
  if (!cd_is_ok(*err))
    goto failed_pread;

  *err = cd_ok();
  return obj;

failed_pread:
  free(obj);

failed_malloc:
  return NULL;
}


void cd_obj_free(cd_obj_t* obj) {
  free(obj);
}


void* cd_obj_get(cd_obj_t* obj, intptr_t addr, size_t size) {
  return NULL;
}


#define CHECKED(expr)                                                         \
    do {                                                                      \
      err = (expr);                                                           \
      if (!cd_is_ok(err))                                                     \
        goto fatal;                                                           \
    } while (0)                                                               \


cd_error_t cd_obj_read_header(cd_obj_t* obj) {
  cd_error_t err;
  size_t hdr_size;
  static char cmd_st[16384];  /* 16kb is usally enough to load the commands */
  char* cmds;
  size_t cmds_size;
  struct {
    size_t file;
    size_t mem_read;
    size_t mem_scan;
  } offs;
  uint32_t left;

  /* Start with a small buffer */
  cmds_size = sizeof(cmd_st);
  cmds = cmd_st;

  hdr_size = obj->is_x64 ? sizeof(obj->hdr.x64) : sizeof(obj->hdr.ia32);
  CHECKED(cd_pread(obj->fd, &obj->hdr.x64, hdr_size, 0, NULL));
  if (obj->hdr.x64.filetype != MH_CORE)
    return cd_error_num(kCDErrNotCore, obj->hdr.x64.filetype);

  /* Iterate through commands */

  left = obj->hdr.x64.ncmds;
  offs.file = hdr_size;
  offs.mem_read = 0;
  offs.mem_scan = 0;
  while (left > 0) {
    size_t res;
    int read;

    CHECKED(cd_pread(obj->fd,
                     cmds + offs.mem_read,
                     cmds_size - offs.mem_read,
                     offs.file,
                     &read));
    offs.file += read;

    /* Try to parse whole buffer */
    read += offs.mem_read;
    while (left > 0 && offs.mem_read < (size_t) read) {
      err = cd_obj_read_cmd(obj,
                            (struct load_command*) (cmds + offs.mem_scan),
                            read - offs.mem_scan,
                            &res);
      if (err.code == kCDErrCmdNotEnough) {
        /* Not enough space, extend cmds and try again */
        if (cmds_size < res) {
          char* ncmds;

          ncmds = malloc(res);
          if (ncmds == NULL) {
            err = cd_error_str(kCDErrNoMem, "obj cmds buf");
            goto fatal;
          }

          memcpy(ncmds, cmds, cmds_size);
          if (cmds != cmd_st)
            free(cmds);
          cmds = ncmds;
          cmds_size = res;
        }

        /* Skip parsed data to free space in a buffer */
        memmove(cmds, cmds + offs.mem_read, read - offs.mem_read);

        /* Read more data from file */
        offs.mem_read = read - offs.mem_read;
        offs.mem_scan = 0;
        break;
      } else if (!cd_is_ok(err)) {
        goto fatal;
      }

      /* Command parsed - move forward */
      offs.mem_scan += res;
      assert(offs.mem_read < offs.mem_scan);
      offs.mem_read = offs.mem_scan;

      left--;
    }

    /* Fully read buffer */
    if (offs.mem_scan >= (size_t) read) {
      offs.mem_scan = 0;
      offs.mem_read = 0;
    }
  }
  return cd_ok();

fatal:
  if (cmds != cmd_st)
    free(cmds);
  return err;
}


cd_error_t cd_obj_read_cmd(cd_obj_t* obj,
                           struct load_command* cmd,
                           size_t size,
                           size_t* res) {
  size_t expected;

  if (cmd->cmdsize == 0)
    return cd_error(kCDErrCmdZeroSize);

  /* Align command sizes */
  *res = cmd->cmdsize;
  if (size < cmd->cmdsize)
    return cd_error(kCDErrCmdNotEnough);

  /* Command is here - parse it */
  switch (cmd->cmd) {
    case LC_SEGMENT:
      expected = sizeof(struct segment_command);
      break;
    case LC_SEGMENT_64:
      expected = sizeof(struct segment_command_64);
      break;
    default:
      expected = 0;
      break;
  }

  if (expected != 0 && expected > size)
    return cd_error_num(kCDErrCmdSmallerThanExpected, expected);

  switch (cmd->cmd) {
    case LC_SEGMENT:
      return cd_obj_read_seg(obj, (struct segment_command*) cmd);
    case LC_SEGMENT_64:
      return cd_obj_read_seg_64(obj, (struct segment_command_64*) cmd);
  }

  return cd_ok();
}


cd_error_t cd_obj_read_seg(cd_obj_t* obj, struct segment_command* cmd) {
  return cd_ok();
}


cd_error_t cd_obj_read_seg_64(cd_obj_t* obj, struct segment_command_64* cmd) {
  return cd_ok();
}

#undef CHECKED
