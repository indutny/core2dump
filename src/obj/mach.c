#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <mach-o/nlist.h>
#include <mach-o/stab.h>
#include <libkern/OSByteOrder.h>

#include "obj/mach.h"
#include "error.h"
#include "obj.h"
#include "obj-internal.h"
#include "common.h"

typedef struct cd_mach_obj_s cd_mach_obj_t;
typedef struct cd_mach_dyld_infos_s cd_mach_dyld_infos_t;
typedef struct cd_mach_dyld_infos32_s cd_mach_dyld_infos32_t;
typedef struct cd_mach_dyld_image_s cd_mach_dyld_image_t;
typedef struct cd_mach_dyld_image32_s cd_mach_dyld_image32_t;
typedef cd_error_t (*cd_mach_obj_iterate_lcmds_cb)(cd_mach_obj_t* obj,
                                                   struct load_command* cmd,
                                                   void* arg);

/* See mach-o/dyld_images.h */
struct cd_mach_dyld_infos_s {
  uint32_t version;
  uint32_t infoArrayCount;
  uint64_t infoArray;
  uint64_t notification;
  uint64_t _reserved0;
  uint64_t loadAddr;

  uint64_t jitInfo;
  uint64_t dyldVersion;
  uint64_t errorMessage;
  uint64_t termFlags;
  uint64_t shmPage;
  uint64_t sysOrderFlag;
  uint64_t uuidArrayCount;
  uint64_t uuidArray;
  uint64_t infosAddress;
  uint64_t initImageCount;
  uint64_t _reserved[5];
  unsigned char uuid[16];
};

struct cd_mach_dyld_infos32_s {
  uint32_t version;
  uint32_t infoArrayCount;
  uint32_t infoArray;
  uint32_t notification;
  uint32_t _reserved0;
  uint32_t loadAddr;

  uint32_t jitInfo;
  uint32_t dyldVersion;
  uint32_t errorMessage;
  uint32_t termFlags;
  uint32_t shmPage;
  uint32_t sysOrderFlag;
  uint32_t uuidArrayCount;
  uint32_t uuidArray;
  uint32_t infosAddress;
  uint32_t initImageCount;
  uint32_t _reserved[5];
  unsigned char uuid[16];
};

struct cd_mach_dyld_image_s {
  uint64_t addr;
  uint64_t path;
  uint64_t mod_date;
};

struct cd_mach_dyld_image32_s {
  uint32_t addr;
  uint32_t path;
  uint32_t mod_date;
};

struct cd_mach_obj_s {
  CD_OBJ_INTERNAL_FIELDS

  struct mach_header* header;
  struct mach_header* dyld;
  uint64_t dyld_off;
  uint64_t dyld_size;
  cd_mach_dyld_infos_t dyld_all_infos;
  char* dyld_path;
  cd_obj_t* dyld_obj;
};


static cd_error_t cd_mach_fat_unwrap(cd_mach_obj_t* obj, cd_obj_opts_t* opts);
static cd_error_t cd_mach_obj_locate(cd_mach_obj_t* obj);
static cd_error_t cd_mach_obj_locate_seg_cb(cd_mach_obj_t* obj,
                                            cd_segment_t* seg);
static cd_error_t cd_mach_obj_locate_final(cd_mach_obj_t* obj);
static cd_error_t cd_mach_obj_iterate_lcmds(cd_mach_obj_t* obj,
                                            struct mach_header* hdr,
                                            size_t size,
                                            cd_mach_obj_iterate_lcmds_cb cb,
                                            void* arg);
static cd_error_t cd_mach_iterate_segs_lcmd_cb(cd_mach_obj_t* obj,
                                               struct load_command* cmd,
                                               void* arg);
static cd_error_t cd_mach_iterate_syms_lcmd_cb(cd_mach_obj_t* obj,
                                               struct load_command* cmd,
                                               void* arg);
static cd_error_t cd_mach_get_thread_lcmd_cb(cd_mach_obj_t* obj,
                                             struct load_command* cmd,
                                             void* arg);
static cd_error_t cd_mach_obj_locate_iterate(cd_mach_obj_t* obj,
                                             struct load_command* cmd,
                                             void* arg);


cd_mach_obj_t* cd_mach_obj_new(int fd, cd_obj_opts_t* opts, cd_error_t* err) {
  cd_mach_obj_t* obj;
  struct stat sbuf;

  obj = malloc(sizeof(*obj));
  if (obj == NULL) {
    *err = cd_error_str(kCDErrNoMem, "cd_mach_obj_t");
    goto failed_malloc;
  }

  /* Just to be able to use cd_obj_ during init */
  obj->method = cd_mach_obj_method;

  /* mmap the file */
  if (fstat(fd, &sbuf) != 0) {
    *err = cd_error_num(kCDErrFStat, errno);
    goto failed_fstat;
  }
  obj->size = sbuf.st_size;

  *err = cd_obj_internal_init((cd_obj_t*) obj);
  if (!cd_is_ok(*err))
    goto failed_fstat;

  if (obj->size < sizeof(*obj->header)) {
    *err = cd_error(kCDErrNotEnoughMagic);
    goto failed_magic;
  }

  obj->addr = mmap(NULL,
                   obj->size,
                   PROT_READ,
                   MAP_FILE | MAP_PRIVATE,
                   fd,
                   0);
  if (obj->addr == MAP_FAILED) {
    *err = cd_error_num(kCDErrMmap, errno);
    goto failed_magic;
  }

  /* Technically the only difference between X64 and IA32 header is a
   * reserved field
   */
  obj->header = obj->addr;

  /* Unwrap FAT file */
  if (obj->header->magic == FAT_CIGAM || obj->header->magic == FAT_MAGIC) {
    *err = cd_mach_fat_unwrap(obj, opts);
    if (!cd_is_ok(*err))
      goto failed_magic2;
  }

  /* Big-Endian not supported */
  if (obj->header->magic == MH_CIGAM ||
      obj->header->magic == MH_CIGAM_64) {
    *err = cd_error_num(kCDErrBigEndianMagic, obj->header->magic);
    goto failed_magic2;
  }

  if (obj->header->magic != MH_MAGIC && obj->header->magic != MH_MAGIC_64) {
    *err = cd_error_num(kCDErrInvalidMagic, obj->header->magic);
    goto failed_magic2;
  }

  obj->is_x64 = obj->header->magic == MH_MAGIC_64;

  /* Find dyld infos */
  if (obj->header->filetype == MH_CORE)
    *err = cd_mach_obj_locate(obj);
  else
    *err = cd_ok();

  if (!cd_is_ok(*err))
    goto failed_magic2;

  return obj;

failed_magic2:
  munmap(obj->addr, obj->size);
  obj->addr = NULL;

failed_magic:
  cd_obj_internal_free((cd_obj_t*) obj);

failed_fstat:
  free(obj);

failed_malloc:
  return NULL;
}


void cd_mach_obj_free(cd_mach_obj_t* obj) {
  munmap(obj->addr, obj->size);
  obj->addr = NULL;

  cd_obj_internal_free((cd_obj_t*) obj);

  free(obj);
}


cd_error_t cd_mach_fat_unwrap(cd_mach_obj_t* obj, cd_obj_opts_t* opts) {
  struct fat_header* fat;
  int swap;
  uint32_t nfat_arch;
  struct fat_arch* arch;

  if (opts == NULL)
    return cd_error_num(kCDErrInvalidMagic, obj->header->magic);

  fat = (struct fat_header*) obj->header;
  swap = fat->magic == FAT_CIGAM;

  nfat_arch = fat->nfat_arch;
  if (swap)
    nfat_arch = OSSwapInt32(nfat_arch);

  arch = (struct fat_arch*) ((char*) fat + sizeof(*fat));
  for (; nfat_arch != 0; nfat_arch--, arch++) {
    cpu_type_t cpu;
    uint32_t off;
    uint32_t size;

    cpu = arch->cputype;
    if (swap)
      cpu = OSSwapInt32(cpu);
    if (((cpu & CPU_ARCH_ABI64) == CPU_ARCH_ABI64) != opts->parent->is_x64)
      continue;

    off = arch->offset;
    size = arch->size;
    if (swap) {
      off = OSSwapInt32(off);
      size = OSSwapInt32(size);
    }

    obj->header = (struct mach_header*) ((char*) obj->header + off);
    obj->size = size;
    return cd_ok();
  }

  return cd_error_str(kCDErrNotFound, "FAT subobject not found");
}


int cd_mach_obj_is_core(cd_mach_obj_t* obj) {
  return obj->header->filetype == MH_CORE;
}


cd_error_t cd_mach_obj_iterate_lcmds(cd_mach_obj_t* obj,
                                     struct mach_header* hdr,
                                     size_t size,
                                     cd_mach_obj_iterate_lcmds_cb cb,
                                     void* arg) {
  size_t sz;
  char* ptr;
  char* end;
  uint32_t left;
  struct load_command* cmd;

  sz = obj->is_x64 ?
      sizeof(struct mach_header_64) :
      sizeof(struct mach_header);

  /* Go through load commands to find matching segment */
  ptr = (char*) hdr + sz;
  end = (char*) hdr + size;
  for (left = hdr->ncmds;
       left > 0;
       ptr += cmd->cmdsize, left--) {
    cd_error_t err;

    if (ptr >= end)
      return cd_error(kCDErrLoadCommandOOB);

    cmd = (struct load_command*) ptr;
    if (ptr + cmd->cmdsize > end)
      return cd_error(kCDErrLoadCommandOOB);

    err = cb(obj, cmd, arg);
    if (!cd_is_ok(err))
      return err;
  }
  return cd_ok();
}


typedef struct cd_mach_iterate_segs_s cd_mach_iterate_segs_t;

struct cd_mach_iterate_segs_s {
  cd_obj_iterate_seg_cb cb;
  void* arg;
};


cd_error_t cd_mach_iterate_segs_lcmd_cb(cd_mach_obj_t* obj,
                                        struct load_command* cmd,
                                        void* arg) {
  uint64_t vmaddr;
  uint64_t vmsize;
  uint64_t fileoff;
  uint64_t sects;
  cd_segment_t seg;
  cd_mach_iterate_segs_t* st;

  st = (cd_mach_iterate_segs_t*) arg;

  if (cmd->cmd == LC_SEGMENT) {
    struct segment_command* seg;

    seg = (struct segment_command*) cmd;

    vmaddr = seg->vmaddr;
    vmsize = seg->vmsize;
    fileoff = seg->fileoff;
    sects = seg->nsects;
  } else if (cmd->cmd == LC_SEGMENT_64) {
    struct segment_command_64* seg;

    seg = (struct segment_command_64*) cmd;

    vmaddr = seg->vmaddr;
    vmsize = seg->vmsize;
    fileoff = seg->fileoff;
    sects = seg->nsects;
  } else {
    /* Continue */
    return cd_ok();
  }

  seg.start = vmaddr;
  seg.end = vmaddr + vmsize;
  seg.fileoff = fileoff;
  seg.sects = sects;
  seg.ptr = (char*) obj->header + fileoff;

  return st->cb((cd_obj_t*) obj, &seg, st->arg);
}


cd_error_t cd_mach_obj_iterate_segs(cd_mach_obj_t* obj,
                                    cd_obj_iterate_seg_cb cb,
                                    void* arg) {
  cd_mach_iterate_segs_t state;

  state.cb = cb;
  state.arg = arg;

  return cd_mach_obj_iterate_lcmds(obj,
                                   obj->header,
                                   obj->size,
                                   cd_mach_iterate_segs_lcmd_cb,
                                   &state);
}


typedef struct cd_mach_iterate_syms_s cd_mach_iterate_syms_t;

struct cd_mach_iterate_syms_s {
  cd_obj_iterate_sym_cb cb;
  void* arg;
};


cd_error_t cd_mach_iterate_syms_lcmd_cb(cd_mach_obj_t* obj,
                                        struct load_command* cmd,
                                        void* arg) {
  char* end;
  struct symtab_command* symtab;
  struct nlist* nl;
  struct nlist_64* nl64;
  size_t nsz;
  unsigned int i;
  cd_mach_iterate_syms_t* st;

  st = (cd_mach_iterate_syms_t*) arg;

  if (cmd->cmd != LC_SYMTAB)
    return cd_ok();

  end = (char*) obj->header + obj->size;

  symtab = (struct symtab_command*) cmd;
  nsz = obj->is_x64 ? sizeof(struct nlist_64) : sizeof(struct nlist);

  if (obj->is_x64)
    nl64 = (struct nlist_64*) ((char*) obj->header + symtab->symoff);
  else
    nl = (struct nlist*) ((char*) obj->header + symtab->symoff);

  for (i = 0; i < symtab->nsyms; i++) {
    cd_error_t err;
    char* name;
    uint8_t type;
    uint64_t value;
    cd_sym_t sym;

    /* XXX Add bounds checks */
    name = (char*) obj->header;
    if (obj->is_x64) {
      if ((char*) &nl64[i] >= end)
        return cd_error(kCDErrSymtabOOB);

      name += symtab->stroff + nl64[i].n_un.n_strx;
      type = nl64[i].n_type;
    } else {
      if ((char*) &nl[i] >= end)
        return cd_error(kCDErrSymtabOOB);

      name += symtab->stroff + nl[i].n_un.n_strx;
      type = nl[i].n_type;
    }

    if (obj->is_x64)
      value = nl64[i].n_value;
    else
      value = nl[i].n_value;

    if (name >= end)
      return cd_error(kCDErrSymtabOOB);

    sym.name = name;
    sym.nlen = strlen(name);
    sym.value = value;
    if (obj->is_x64)
      sym.sect = nl64[i].n_sect;
    else
      sym.sect = nl[i].n_sect;

    err = st->cb((cd_obj_t*) obj, &sym, st->arg);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


cd_error_t cd_mach_obj_iterate_syms(cd_mach_obj_t* obj,
                                    cd_obj_iterate_sym_cb cb,
                                    void* arg) {
  cd_error_t err;
  cd_mach_iterate_syms_t state;

  state.cb = cb;
  state.arg = arg;

  err = cd_mach_obj_iterate_lcmds(obj,
                                  obj->header,
                                  obj->size,
                                  cd_mach_iterate_syms_lcmd_cb,
                                  &state);
  if (err.code == kCDErrSkip)
    return cd_ok();
  else
    return err;
}


typedef struct cd_mach_get_thread_s cd_mach_get_thread_t;

struct cd_mach_get_thread_s {
  cd_obj_thread_t* thread;
  unsigned int index;
};


cd_error_t cd_mach_get_thread_lcmd_cb(cd_mach_obj_t* obj,
                                      struct load_command* cmd,
                                      void* arg) {
  cd_mach_get_thread_t* st;
  cd_obj_thread_t* thread;
  char* ptr;
  size_t size;
  char* end;

  st = (cd_mach_get_thread_t*) arg;
  thread = st->thread;

  if (cmd->cmd != LC_THREAD)
    return cd_ok();

  end = (char*) cmd + cmd->cmdsize;
  for (ptr = (char*) cmd + 8; ptr < end; ptr += size) {
    uint32_t flavor;
    uint32_t count;
    struct x86_thread_state* state;

    flavor = *((uint32_t*) ptr);
    count = *((uint32_t*) ptr + 1);
    size = 8 + 4 * count;

    /* Thread state is too big */
    if (ptr + size > end)
      return cd_error(kCDErrThreadStateOOB);

    if (flavor != x86_THREAD_STATE)
      continue;

    if (count != x86_THREAD_STATE_COUNT)
      return cd_error(kCDErrThreadStateInvalidSize);

    /* We are looking for different index */
    if (st->index != 0) {
      st->index--;
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

      thread->regs.ip = state->uts.ts64.__rip;
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

      thread->regs.ip = state->uts.ts32.__eip;
      thread->stack.top = state->uts.ts32.__esp;
      thread->stack.frame = state->uts.ts32.__ebp;
      thread->stack.bottom = 0xc0000000;
    }

    return cd_error(kCDErrSkip);
  }

  return cd_ok();
}


cd_error_t cd_mach_obj_get_thread(cd_mach_obj_t* obj,
                                  unsigned int index,
                                  cd_obj_thread_t* thread) {
  cd_error_t err;
  cd_mach_get_thread_t state;

  if (!cd_mach_obj_is_core(obj))
    return cd_error_num(kCDErrNotCore, obj->header->filetype);

  state.index = index;
  state.thread = thread;

  err = cd_mach_obj_iterate_lcmds(obj,
                                  obj->header,
                                  obj->size,
                                  cd_mach_get_thread_lcmd_cb,
                                  &state);
  if (err.code == kCDErrSkip)
    return cd_ok();
  else if (!cd_is_ok(err))
    return err;

  return cd_error(kCDErrNotFound);
}


cd_error_t cd_mach_obj_locate_seg_cb(cd_mach_obj_t* obj, cd_segment_t* seg) {
  uint64_t off;
  uint64_t size;

  /* Found everything - no point in continuing */
  if (obj->dyld != NULL)
    return cd_error(kCDErrSkip);

  size = seg->end - seg->start;
  for (off = 0; off < size; off += 0x1000) {
    struct mach_header* hdr;

    hdr = (struct mach_header*) (seg->ptr + off);
    if (hdr->magic != MH_MAGIC && hdr->magic != MH_MAGIC_64)
      continue;

    if (hdr->filetype == MH_DYLINKER && obj->dyld == NULL) {
      obj->dyld = hdr;
      obj->dyld_off = seg->start + off;
      obj->dyld_size = size - off;
      break;
    }
  }
  return cd_ok();
}


cd_error_t cd_mach_obj_locate_iterate(cd_mach_obj_t* obj,
                                      struct load_command* cmd,
                                      void* arg) {
  struct dylinker_command* dcmd;

  if (cmd->cmd != LC_ID_DYLINKER)
    return cd_ok();

  dcmd = (struct dylinker_command*) cmd;
  obj->dyld_path = (char*) dcmd + dcmd->name.offset;

  return cd_error(kCDErrSkip);
};


cd_error_t cd_mach_obj_locate(cd_mach_obj_t* obj) {
  cd_error_t err;
  cd_obj_opts_t opts;

  obj->dyld = NULL;
  obj->dyld_path = NULL;
  obj->dyld_obj = NULL;

  err = cd_mach_obj_method->obj_iterate_segs(
      (cd_obj_t*) obj,
      (cd_obj_iterate_seg_cb) cd_mach_obj_locate_seg_cb,
      NULL);

  if (!cd_is_ok(err) && err.code != kCDErrSkip)
    goto fatal;

  if (obj->dyld == NULL) {
    err = cd_error_str(kCDErrNotFound, "dylinker not found");
    goto fatal;
  }

  err = cd_mach_obj_iterate_lcmds(obj,
                                  obj->dyld,
                                  obj->dyld_size,
                                  cd_mach_obj_locate_iterate,
                                  NULL);
  if (!cd_is_ok(err) && err.code != kCDErrSkip)
    goto fatal;

  if (obj->dyld_path == NULL) {
    err = cd_error_str(kCDErrNotFound, "dyld path not found");
    goto fatal;
  }

  opts.parent = (cd_obj_t*) obj;
  opts.reloc = obj->dyld_off;
  obj->dyld_obj = cd_obj_new_ex(cd_mach_obj_method,
                                obj->dyld_path,
                                &opts,
                                &err);
  if (!cd_is_ok(err))
    goto fatal;

  err = cd_mach_obj_locate_final(obj);
  if (!cd_is_ok(err))
    goto failed_get_sym;

  return cd_ok();

failed_get_sym:
  cd_obj_free(obj->dyld_obj);

fatal:
  return err;
}


cd_error_t cd_mach_obj_locate_final(cd_mach_obj_t* obj) {
  cd_error_t err;
  uint64_t infos_off;
  cd_mach_dyld_infos_t* infos;
  uint64_t off;
  uint64_t info_delta;
  char* info_array;

  err = cd_obj_get_sym(obj->dyld_obj, "_dyld_all_image_infos", &infos_off);
  if (!cd_is_ok(err))
    return err;

  if (obj->is_x64) {
    cd_mach_dyld_infos_t* infos;

    err = cd_obj_get((cd_obj_t*) obj,
                     infos_off,
                     sizeof(*infos),
                     (void**) &infos);
    if (!cd_is_ok(err))
      return err;

    obj->dyld_all_infos = *infos;
  } else {
    cd_mach_dyld_infos32_t* infos;

    err = cd_obj_get((cd_obj_t*) obj,
                     infos_off,
                     sizeof(*infos),
                     (void**) &infos);
    if (!cd_is_ok(err))
      return err;

    obj->dyld_all_infos.version = infos->version;
    obj->dyld_all_infos.infoArrayCount = infos->infoArrayCount;
    obj->dyld_all_infos.infoArray = infos->infoArray;
    obj->dyld_all_infos.notification = infos->notification;
    obj->dyld_all_infos.loadAddr = infos->loadAddr;
    obj->dyld_all_infos.jitInfo = infos->jitInfo;
    obj->dyld_all_infos.dyldVersion = infos->dyldVersion;
    obj->dyld_all_infos.errorMessage = infos->errorMessage;
    obj->dyld_all_infos.termFlags = infos->termFlags;
    obj->dyld_all_infos.shmPage = infos->shmPage;
    obj->dyld_all_infos.sysOrderFlag = infos->sysOrderFlag;
    obj->dyld_all_infos.uuidArrayCount = infos->uuidArrayCount;
    obj->dyld_all_infos.uuidArray = infos->uuidArray;
    obj->dyld_all_infos.infosAddress = infos->infosAddress;
    obj->dyld_all_infos.initImageCount = infos->initImageCount;
    memcpy(obj->dyld_all_infos.uuid, infos->uuid, sizeof(infos->uuid));
  }
  infos = &obj->dyld_all_infos;
  if (obj->is_x64)
    info_delta = sizeof(cd_mach_dyld_image_t);
  else
    info_delta = sizeof(cd_mach_dyld_image32_t);

  err = cd_obj_get((cd_obj_t*) obj,
                   infos->infoArray,
                   info_delta * infos->infoArrayCount,
                   (void**) &info_array);
  if (!cd_is_ok(err))
    return err;

  for (off = 0; off < infos->infoArrayCount; off++, info_array += info_delta) {
    uint64_t addr;
    uint64_t path;
    char* cpath;
    cd_obj_t* image;
    cd_obj_opts_t opts;

    if (obj->is_x64) {
      cd_mach_dyld_image_t* image;

      image = (cd_mach_dyld_image_t*) info_array;
      addr = image->addr;
      path = image->path;
    } else {
      cd_mach_dyld_image32_t* image;

      image = (cd_mach_dyld_image32_t*) info_array;
      addr = image->addr;
      path = image->path;
    }

    if (path == 0)
      continue;

    err = cd_obj_get((cd_obj_t*) obj, path, 1, (void**) &cpath);
    if (!cd_is_ok(err))
      return err;

    opts.parent = (cd_obj_t*) obj;
    opts.reloc = addr;
    image = cd_obj_new_ex(cd_mach_obj_method, cpath, &opts, &err);
    /* Ignore errors */
    /* TODO(indutny): print warnings? */
  }

  return cd_ok();
}


typedef struct cd_mach_obj_get_dbg_s cd_mach_obj_get_dbg_t;

struct cd_mach_obj_get_dbg_s {
  void** res;
  uint64_t* size;
  uint64_t* vmaddr;
};


cd_error_t cd_mach_iterate_dbg_lcmd_cb(cd_mach_obj_t* obj,
                                       struct load_command* cmd,
                                       void* arg) {
  cd_mach_obj_get_dbg_t* st;
  struct segment_command_64* seg;
  struct segment_command_64 seg_st;
  struct section_64* sect64;
  struct section* sect32;
  uint32_t i;

  st = (cd_mach_obj_get_dbg_t*) arg;
  if (cmd->cmd == LC_SEGMENT) {
    struct segment_command* seg32;

    seg32 = (struct segment_command*) cmd;
    seg = &seg_st;

    seg->cmd = seg32->cmd;
    seg->cmdsize = seg32->cmdsize;
    memcpy(seg->segname, seg32->segname, sizeof(seg32->segname));
    seg->vmaddr = seg32->vmaddr;
    seg->vmsize = seg32->vmsize;
    seg->fileoff = seg32->fileoff;
    seg->filesize = seg32->filesize;
    seg->maxprot = seg32->maxprot;
    seg->initprot = seg32->initprot;
    seg->nsects = seg32->nsects;
    seg->flags = seg32->flags;

    sect32 = (struct section*) ((char*) cmd + sizeof(*seg32));
  } else if (cmd->cmd == LC_SEGMENT_64) {
    seg = (struct segment_command_64*) cmd;

    sect64 = (struct section_64*) ((char*) cmd + sizeof(*seg));
  } else {
    return cd_ok();
  }

  if (strcmp(seg->segname, "__TEXT") != 0)
    return cd_ok();

  for (i = 0; i < seg->nsects; i++, sect64++, sect32++) {
    struct section_64* sect;
    struct section_64 sect_st;

    if (obj->is_x64) {
      sect = sect64;
    } else {
      sect = &sect_st;

      memcpy(sect->sectname, sect32->sectname, sizeof(sect32->sectname));
      memcpy(sect->segname, sect32->segname, sizeof(sect32->segname));

      sect->addr = sect32->addr;
      sect->size = sect32->size;
      sect->offset = sect32->offset;
      sect->align = sect32->align;
      sect->reloff = sect32->reloff;
      sect->nreloc = sect32->nreloc;
      sect->flags = sect32->flags;
    }

    if (strcmp(sect->sectname, "__eh_frame") != 0)
      continue;

    *st->res = (char*) obj->header + sect->offset;
    *st->size = sect->size;
    *st->vmaddr = sect->addr;
    return cd_error(kCDErrSkip);
  }

  return cd_ok();
}


cd_error_t cd_mach_obj_get_dbg(cd_mach_obj_t* obj,
                               void** res,
                               uint64_t* size,
                               uint64_t* vmaddr) {
  cd_error_t err;
  cd_mach_obj_get_dbg_t state;

  state.res = res;
  state.size = size;
  state.vmaddr = vmaddr;

  err = cd_mach_obj_iterate_lcmds(obj,
                                  obj->header,
                                  obj->size,
                                  cd_mach_iterate_dbg_lcmd_cb,
                                  &state);
  if (err.code == kCDErrSkip)
    return cd_ok();
  if (!cd_is_ok(err))
    return err;

  return cd_error_str(kCDErrNotFound, "__eh_frame not found");
}


cd_obj_method_t cd_mach_obj_method_def = {
  .obj_new = (cd_obj_method_new_t) cd_mach_obj_new,
  .obj_free = (cd_obj_method_free_t) cd_mach_obj_free,
  .obj_is_core = (cd_obj_method_is_core_t) cd_mach_obj_is_core,
  .obj_get_thread = (cd_obj_method_get_thread_t) cd_mach_obj_get_thread,
  .obj_iterate_syms = (cd_obj_method_iterate_syms_t) cd_mach_obj_iterate_syms,
  .obj_iterate_segs = (cd_obj_method_iterate_segs_t) cd_mach_obj_iterate_segs,
  .obj_get_dbg_frame = (cd_obj_method_get_dbg_frame_t) cd_mach_obj_get_dbg
};

cd_obj_method_t* cd_mach_obj_method = &cd_mach_obj_method_def;
