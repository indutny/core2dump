#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>

#include "error.h"
#include "obj.h"
#include "common.h"


struct cd_obj_s {
  void* addr;
  size_t size;
  int is_x64;
  struct mach_header* header;
  cd_hashmap_t syms;
  int has_syms;
};


cd_obj_t* cd_obj_new(int fd, cd_error_t* err) {
  cd_obj_t* obj;
  struct stat sbuf;

  obj = malloc(sizeof(*obj));
  if (obj == NULL) {
    *err = cd_error_str(kCDErrNoMem, "cd_obj_t");
    goto failed_malloc;
  }

  /* mmap the file */
  if (fstat(fd, &sbuf) != 0) {
    *err = cd_error_num(kCDErrFStat, errno);
    goto failed_fstat;
  }
  obj->size = sbuf.st_size;
  obj->has_syms = 0;

  if (obj->size < sizeof(*obj->header)) {
    *err = cd_error(kCDErrNotEnoughMagic);
    goto failed_fstat;
  }

  obj->addr = mmap(NULL,
                   obj->size,
                   PROT_READ,
                   MAP_FILE | MAP_PRIVATE,
                   fd,
                   0);
  if (obj->addr == MAP_FAILED) {
    *err = cd_error_num(kCDErrMmap, errno);
    goto failed_fstat;
  }

  /* Technically the only difference between X64 and IA32 header is a
   * reserved field
   */
  obj->header = obj->addr;

  /* Big-Endian not supported */
  if (obj->header->magic == MH_CIGAM || obj->header->magic == MH_CIGAM_64) {
    *err = cd_error_num(kCDErrBigEndianMagic, obj->header->magic);
    goto failed_magic_check;
  }

  if (obj->header->magic != MH_MAGIC && obj->header->magic != MH_MAGIC_64) {
    *err = cd_error_num(kCDErrInvalidMagic, obj->header->magic);
    goto failed_magic_check;
  }

  obj->is_x64 = obj->header->magic == MH_MAGIC_64;

  *err = cd_ok();
  return obj;

failed_magic_check:
  munmap(obj->addr, obj->size);
  obj->addr = NULL;

failed_fstat:
  free(obj);

failed_malloc:
  return NULL;
}


void cd_obj_free(cd_obj_t* obj) {
  munmap(obj->addr, obj->size);
  obj->addr = NULL;

  if (obj->has_syms)
    cd_hashmap_destroy(&obj->syms);
  obj->has_syms = 0;

  free(obj);
}


int cd_obj_is_x64(cd_obj_t* obj) {
  return obj->is_x64;
}


#define CD_ITERATE_LCMDS(BODY)                                                \
    do {                                                                      \
      size_t sz;                                                              \
      char* ptr;                                                              \
      char* end;                                                              \
      uint32_t left;                                                          \
      struct load_command* cmd;                                               \
      sz = obj->is_x64 ?                                                      \
          sizeof(struct mach_header_64) :                                     \
          sizeof(struct mach_header);                                         \
      /* Go through load commands to find matching segment */                 \
      ptr = (char*) obj->addr + sz;                                           \
      end = (char*) obj->addr + obj->size;                                    \
      left = obj->header->ncmds;                                              \
      for (left = obj->header->ncmds;                                         \
           left > 0;                                                          \
           ptr += cmd->cmdsize, left--) {                                     \
        if (ptr >= end) {                                                     \
          err = cd_error(kCDErrLoadCommandOOB);                               \
          goto fatal;                                                         \
        }                                                                     \
        cmd = (struct load_command*) ptr;                                     \
        if (ptr + cmd->cmdsize > end) {                                       \
          err = cd_error(kCDErrLoadCommandOOB);                               \
          goto fatal;                                                         \
        }                                                                     \
        do BODY while(0);                                                     \
      }                                                                       \
    } while (0);                                                              \


cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res) {
  cd_error_t err;

  if (obj->header->filetype != MH_CORE) {
    err = cd_error_num(kCDErrNotCore, obj->header->filetype);
    goto fatal;
  }

  CD_ITERATE_LCMDS({
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    char* r;

    if (cmd->cmd == LC_SEGMENT) {
      struct segment_command* seg;

      seg = (struct segment_command*) cmd;

      vmaddr = seg->vmaddr;
      vmsize = seg->vmsize;
      fileoff = seg->fileoff;
    } else if (cmd->cmd == LC_SEGMENT_64) {
      struct segment_command_64* seg;

      seg = (struct segment_command_64*) cmd;

      vmaddr = seg->vmaddr;
      vmsize = seg->vmsize;
      fileoff = seg->fileoff;
    } else {
      continue;
    }

    if (vmaddr > addr || vmsize + vmaddr < addr + size)
      continue;

    r = (char*) obj->addr + fileoff + (addr - vmaddr);
    if (r + size > end) {
      err = cd_error(kCDErrLoadCommandOOB);
      goto fatal;
    }

    *res = r;
    return cd_ok();
  })

  *res = NULL;
  err = cd_error(kCDErrNotFound);

fatal:
  return err;
}


cd_error_t cd_obj_get_sym(cd_obj_t* obj, const char* sym, uint64_t* addr) {
  cd_error_t err;
  void* res;

  if (obj->has_syms)
    goto lookup;

  CD_ITERATE_LCMDS({
    struct symtab_command* symtab;
    struct nlist* nl;
    struct nlist_64* nl64;
    size_t nsz;
    unsigned int i;

    if (cmd->cmd != LC_SYMTAB)
      continue;

    symtab = (struct symtab_command*) cmd;
    nsz = obj->is_x64 ? sizeof(struct nlist_64) : sizeof(struct nlist);
    if (symtab->symoff + symtab->nsyms * nsz > obj->size) {
      err = cd_error(kCDErrSymtabOOB);
      goto fatal;
    }

    if (cd_hashmap_init(&obj->syms, symtab->nsyms * 4, 0) != 0) {
      err = cd_error_str(kCDErrNoMem, "cd_hashmap_t");
      goto fatal;
    }
    obj->has_syms = 1;

    if (obj->is_x64)
      nl64 = (struct nlist_64*) ((char*) obj->addr + symtab->symoff);
    else
      nl = (struct nlist*) ((char*) obj->addr + symtab->symoff);

    for (i = 0; i < symtab->nsyms; i++) {
      char* name;
      int len;
      uint8_t type;
      uint64_t value;

      /* XXX Add bounds checks */
      name = obj->addr;
      if (obj->is_x64) {
        name += symtab->stroff + nl64[i].n_un.n_strx;
        type = nl64[i].n_type;
      } else {
        name += symtab->stroff + nl[i].n_un.n_strx;
        type = nl[i].n_type;
      }
      type &= N_TYPE;

      if (obj->is_x64)
        value = nl64[i].n_value;
      else
        value = nl[i].n_value;
      if (value == 0)
        continue;

      len = strlen(name);
      if (len == 0)
        continue;

      if (cd_hashmap_insert(&obj->syms, name, len, (void*) value) != 0) {
        err = cd_error_str(kCDErrNoMem, "cd_hashmap_insert");
        goto fatal;
      }
    }

    break;
  })
  if (!obj->has_syms) {
    err = cd_error(kCDErrNotFound);
    goto fatal;
  }

lookup:
  assert(sizeof(void*) == sizeof(*addr));
  res = cd_hashmap_get(&obj->syms, sym, strlen(sym));
  if (res == NULL) {
    err = cd_error(kCDErrNotFound);
  } else {
    *addr = (uint64_t) res;
    err = cd_ok();
  }

fatal:
  return err;
}


cd_error_t cd_obj_iterate(cd_obj_t* obj, cd_obj_iterate_cb cb, void* arg) {
  cd_error_t err;

  if (obj->header->filetype != MH_CORE) {
    err = cd_error_num(kCDErrNotCore, obj->header->filetype);
    goto fatal;
  }

  CD_ITERATE_LCMDS({
    uint64_t vmsize;
    uint64_t fileoff;

    if (cmd->cmd == LC_SEGMENT) {
      struct segment_command* seg;

      seg = (struct segment_command*) cmd;

      vmsize = seg->vmsize;
      fileoff = seg->fileoff;
    } else if (cmd->cmd == LC_SEGMENT_64) {
      struct segment_command_64* seg;

      seg = (struct segment_command_64*) cmd;

      vmsize = seg->vmsize;
      fileoff = seg->fileoff;
    } else {
      continue;
    }

    err = cb(arg, (char*) obj->addr + fileoff, vmsize);
    if (cd_is_ok(err))
      return err;
    else if (err.code != kCDErrNotFound)
      return err;
  })

  err = cd_error(kCDErrNotFound);

fatal:
  return err;
}


cd_error_t cd_obj_get_thread(cd_obj_t* obj,
                             unsigned int index,
                             cd_obj_thread_t* thread) {
  cd_error_t err;

  if (obj->header->filetype != MH_CORE) {
    err = cd_error_num(kCDErrNotCore, obj->header->filetype);
    goto fatal;
  }

  CD_ITERATE_LCMDS({
    char* ptr;
    size_t size;
    char* end;

    if (cmd->cmd != LC_THREAD)
      continue;

    end = (char*) cmd + cmd->cmdsize;
    for (ptr = (char*) cmd + 8; ptr < end; ptr += size) {
      uint32_t flavor;
      uint32_t count;
      struct x86_thread_state* state;

      flavor = *((uint32_t*) ptr);
      count = *((uint32_t*) ptr + 1);
      size = 8 + 4 * count;

      /* Thread state is too big */
      if (ptr + size > end) {
        err = cd_error(kCDErrThreadStateOOB);
        goto fatal;
      }

      if (flavor != x86_THREAD_STATE)
        continue;

      if (count != x86_THREAD_STATE_COUNT) {
        err = cd_error(kCDErrThreadStateInvalidSize);
        goto fatal;
      }

      /* We are looking for different index */
      if (index != 0) {
        index--;
        continue;
      }

      state = (struct x86_thread_state*) (ptr + 8);
      if (obj->is_x64) {
        thread->regs.count = 21;
        thread->regs.values[0] = state->uts.ts64.__rax;
        thread->regs.values[1] = state->uts.ts64.__rbx;
        thread->regs.values[2] = state->uts.ts64.__rcx;
        thread->regs.values[3] = state->uts.ts64.__rdx;
        thread->regs.values[4] = state->uts.ts64.__rdi;
        thread->regs.values[5] = state->uts.ts64.__rsi;
        thread->regs.values[6] = state->uts.ts64.__rbp;
        thread->regs.values[7] = state->uts.ts64.__rsp;
        thread->regs.values[8] = state->uts.ts64.__r8;
        thread->regs.values[9] = state->uts.ts64.__r9;
        thread->regs.values[10] = state->uts.ts64.__r10;
        thread->regs.values[11] = state->uts.ts64.__r11;
        thread->regs.values[12] = state->uts.ts64.__r12;
        thread->regs.values[13] = state->uts.ts64.__r13;
        thread->regs.values[14] = state->uts.ts64.__r14;
        thread->regs.values[15] = state->uts.ts64.__r15;
        thread->regs.values[16] = state->uts.ts64.__rip;
        thread->regs.values[17] = state->uts.ts64.__rflags;
        thread->regs.values[18] = state->uts.ts64.__cs;
        thread->regs.values[19] = state->uts.ts64.__fs;
        thread->regs.values[20] = state->uts.ts64.__gs;

        thread->stack.top = state->uts.ts64.__rsp;
        thread->stack.frame = state->uts.ts64.__rbp;
        thread->stack.bottom = 0x7fff5fc00000LL;
      } else {
        thread->regs.count = 16;
        thread->regs.values[0] = state->uts.ts32.__eax;
        thread->regs.values[1] = state->uts.ts32.__ebx;
        thread->regs.values[2] = state->uts.ts32.__ecx;
        thread->regs.values[3] = state->uts.ts32.__edx;
        thread->regs.values[4] = state->uts.ts32.__edi;
        thread->regs.values[5] = state->uts.ts32.__esi;
        thread->regs.values[6] = state->uts.ts32.__ebp;
        thread->regs.values[7] = state->uts.ts32.__esp;
        thread->regs.values[8] = state->uts.ts32.__ss;
        thread->regs.values[9] = state->uts.ts32.__eflags;
        thread->regs.values[10] = state->uts.ts32.__eip;
        thread->regs.values[11] = state->uts.ts32.__cs;
        thread->regs.values[12] = state->uts.ts32.__ds;
        thread->regs.values[13] = state->uts.ts32.__es;
        thread->regs.values[14] = state->uts.ts32.__fs;
        thread->regs.values[15] = state->uts.ts32.__gs;

        thread->stack.top = state->uts.ts32.__esp;
        thread->stack.frame = state->uts.ts32.__ebp;
        thread->stack.bottom = 0xc0000000;
      }

      return cd_ok();
    }
  });

  err = cd_error(kCDErrNotFound);

fatal:
  return err;
}


#undef CD_ITERATE_LCMDS
