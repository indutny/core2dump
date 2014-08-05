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
typedef struct cd_mach_opts_s cd_mach_opts_t;
typedef struct cd_mach_dyld_infos_s cd_mach_dyld_infos_t;
typedef struct cd_mach_dyld_infos32_s cd_mach_dyld_infos32_t;
typedef struct cd_mach_dyld_image_s cd_mach_dyld_image_t;
typedef struct cd_mach_dyld_image32_s cd_mach_dyld_image32_t;

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


struct cd_mach_opts_s {
  cd_mach_obj_t* parent;
  uint64_t reloc;
};

static cd_error_t cd_mach_fat_unwrap(cd_mach_obj_t* obj, cd_mach_opts_t* opts);
static cd_error_t cd_mach_obj_locate(cd_mach_obj_t* obj);
static cd_error_t cd_mach_obj_locate_seg_cb(cd_mach_obj_t* obj,
                                            cd_segment_t* seg);
static cd_error_t cd_mach_obj_locate_final(cd_mach_obj_t* obj);
static cd_error_t cd_mach_obj_init_aslr(cd_mach_obj_t* obj,
                                        cd_mach_opts_t* opts);


cd_mach_obj_t* cd_mach_obj_new(int fd, cd_mach_opts_t* opts, cd_error_t* err) {
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

  /* Link DSOs */
  if (opts != NULL) {
    *err = cd_mach_obj_init_aslr(obj, opts);
    if (!cd_is_ok(*err))
      goto failed_magic2;

    QUEUE_INSERT_TAIL(&opts->parent->dso, &obj->member);
  }

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


cd_error_t cd_mach_obj_init_aslr(cd_mach_obj_t* obj, cd_mach_opts_t* opts) {
  cd_error_t err;
  int i;

  /* Figure out the ASLR slide value */
  err = cd_obj_init_segments((cd_obj_t*) obj);
  if (!cd_is_ok(err))
    return err;

  for (i = 0; i < obj->segment_count; i++) {
    cd_segment_t* seg;

    seg = &obj->segments[i];
    if (seg->fileoff != 0 || seg->sects == 0)
      continue;

    obj->aslr = (int64_t) opts->reloc - seg->start;
    break;
  }

  if (i == obj->segment_count)
    return cd_error_str(kCDErrNotFound, "0-fileoff dyld segment");

  return cd_ok();
}


cd_error_t cd_mach_fat_unwrap(cd_mach_obj_t* obj, cd_mach_opts_t* opts) {
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


#define CD_ITERATE_LCMDS(HEADER, SIZE, BODY)                                  \
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
      ptr = (char*) (HEADER) + sz;                                            \
      end = (char*) (HEADER) + (SIZE);                                        \
      for (left = (HEADER)->ncmds;                                            \
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


cd_error_t cd_mach_obj_iterate_segs(cd_mach_obj_t* obj,
                                    cd_obj_iterate_seg_cb cb,
                                    void* arg) {
  cd_error_t err;

  CD_ITERATE_LCMDS(obj->header, obj->size, {
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t sects;
    cd_segment_t seg;

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
      continue;
    }

    seg.start = vmaddr;
    seg.end = vmaddr + vmsize;
    seg.fileoff = fileoff;
    seg.sects = sects;
    seg.ptr = (char*) obj->addr + fileoff;

    /* Fill the splay tree */
    err = cb((cd_obj_t*) obj, &seg, arg);
    if (!cd_is_ok(err))
      goto fatal;
  })

  return cd_ok();

fatal:
  return err;
}


cd_error_t cd_mach_obj_iterate_syms(cd_mach_obj_t* obj,
                                    cd_obj_iterate_sym_cb cb,
                                    void* arg) {
  cd_error_t err;
  char* end;

  end = (char*) obj->header + obj->size;

  CD_ITERATE_LCMDS(obj->header, obj->size, {
    struct symtab_command* symtab;
    struct nlist* nl;
    struct nlist_64* nl64;
    size_t nsz;
    unsigned int i;

    if (cmd->cmd != LC_SYMTAB)
      continue;

    symtab = (struct symtab_command*) cmd;
    nsz = obj->is_x64 ? sizeof(struct nlist_64) : sizeof(struct nlist);

    if (obj->is_x64)
      nl64 = (struct nlist_64*) ((char*) obj->header + symtab->symoff);
    else
      nl = (struct nlist*) ((char*) obj->header + symtab->symoff);

    for (i = 0; i < symtab->nsyms; i++) {
      char* name;
      uint8_t type;
      uint64_t value;
      cd_sym_t sym;

      /* XXX Add bounds checks */
      name = (char*) obj->header;
      if (obj->is_x64) {
        if ((char*) &nl64[i] >= end) {
          err = cd_error(kCDErrSymtabOOB);
          goto fatal;
        }

        name += symtab->stroff + nl64[i].n_un.n_strx;
        type = nl64[i].n_type;
      } else {
        if ((char*) &nl[i] >= end) {
          err = cd_error(kCDErrSymtabOOB);
          goto fatal;
        }

        name += symtab->stroff + nl[i].n_un.n_strx;
        type = nl[i].n_type;
      }

      /* Only external/global symbols are allowed */
      if ((type & N_EXT) == 0)
        continue;

      if (obj->is_x64)
        value = nl64[i].n_value;
      else
        value = nl[i].n_value;

      if (name >= end) {
        err = cd_error(kCDErrSymtabOOB);
        goto fatal;
      }

      sym.name = name;
      sym.nlen = strlen(name);
      sym.value = value;
      if (obj->is_x64)
        sym.sect = nl64[i].n_sect;
      else
        sym.sect = nl[i].n_sect;

      err = cb((cd_obj_t*) obj, &sym, arg);
      if (!cd_is_ok(err))
        goto fatal;
    }

    break;
  })
  return cd_ok();

fatal:
  return err;
}


cd_error_t cd_mach_obj_get_thread(cd_mach_obj_t* obj,
                                  unsigned int index,
                                  cd_obj_thread_t* thread) {
  cd_error_t err;

  if (!cd_mach_obj_is_core(obj)) {
    err = cd_error_num(kCDErrNotCore, obj->header->filetype);
    goto fatal;
  }

  CD_ITERATE_LCMDS(obj->header, obj->size, {
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

      return cd_ok();
    }
  });

  err = cd_error(kCDErrNotFound);

fatal:
  return err;
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


cd_error_t cd_mach_obj_locate(cd_mach_obj_t* obj) {
  cd_error_t err;
  cd_mach_opts_t opts;

  obj->dyld = NULL;
  obj->dyld_path = NULL;
  obj->dyld_obj = NULL;

  err = cd_mach_obj_method->obj_iterate_segs(
      (cd_obj_t*) obj,
      (cd_obj_iterate_seg_cb) cd_mach_obj_locate_seg_cb,
      NULL);

  if (obj->dyld == NULL) {
    err = cd_error_str(kCDErrNotFound, "dylinker not found");
    goto fatal;
  }

  if (err.code != kCDErrSkip)
    goto fatal;

  CD_ITERATE_LCMDS(obj->dyld, obj->dyld_size, {
    struct dylinker_command* dcmd;

    if (cmd->cmd != LC_ID_DYLINKER)
      continue;

    dcmd = (struct dylinker_command*) cmd;
    obj->dyld_path = (char*) dcmd + dcmd->name.offset;
  });

  if (obj->dyld_path == NULL) {
    err = cd_error_str(kCDErrNotFound, "dyld path not found");
    goto fatal;
  }

  opts.parent = obj;
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
    cd_mach_opts_t opts;

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

    opts.parent = obj;
    opts.reloc = addr;
    image = cd_obj_new_ex(cd_mach_obj_method, cpath, &opts, &err);
    /* Ignore errors */
    /* TODO(indutny): print warnings? */
  }

  return cd_ok();
}


cd_obj_method_t cd_mach_obj_method_def = {
  .obj_new = (cd_obj_method_new_t) cd_mach_obj_new,
  .obj_free = (cd_obj_method_free_t) cd_mach_obj_free,
  .obj_is_core = (cd_obj_method_is_core_t) cd_mach_obj_is_core,
  .obj_get_thread = (cd_obj_method_get_thread_t) cd_mach_obj_get_thread,
  .obj_iterate_syms = (cd_obj_method_iterate_syms_t) cd_mach_obj_iterate_syms,
  .obj_iterate_segs = (cd_obj_method_iterate_segs_t) cd_mach_obj_iterate_segs
};

cd_obj_method_t* cd_mach_obj_method = &cd_mach_obj_method_def;


#undef CD_ITERATE_LCMDS
