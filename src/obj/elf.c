#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(__linux__)
#include <linux/elf.h>
#else
#include <elf.h>
#endif

#include "obj/elf.h"
#include "error.h"
#include "obj.h"
#include "common.h"
#include "obj-internal.h"

typedef struct cd_elf_obj_s cd_elf_obj_t;

typedef cd_error_t (*cd_elf_obj_iterate_sh_cb)(cd_elf_obj_t* obj,
                                               Elf64_Shdr* shdr,
                                               void* arg);
typedef cd_error_t (*cd_elf_obj_iterate_notes_cb)(cd_elf_obj_t* obj,
                                                  Elf64_Nhdr* nhdr,
                                                  char* desc,
                                                  void* arg);

static cd_error_t cd_elf_obj_get_section(cd_elf_obj_t* obj,
                                         const char* name,
                                         char** sect,
                                         uint64_t* size,
                                         uint64_t* addr);
static cd_error_t cd_elf_obj_load_dsos(cd_elf_obj_t* obj);
static cd_error_t cd_elf_obj_iterate_sh(cd_elf_obj_t* obj,
                                        cd_elf_obj_iterate_sh_cb cb,
                                        void* arg);
static cd_error_t cd_elf_obj_iterate_notes(cd_elf_obj_t* obj,
                                           cd_elf_obj_iterate_notes_cb cb,
                                           void* arg);
static cd_error_t cd_elf_obj_load_dsos_iterate(cd_elf_obj_t* obj,
                                               Elf64_Nhdr* nhdr,
                                               char* desc,
                                               void* arg);


struct cd_elf_obj_s {
  CD_OBJ_INTERNAL_FIELDS

  Elf64_Ehdr header;
  Elf64_Ehdr* h64;
  Elf32_Ehdr* h32;
  const char* shstrtab;
};


cd_elf_obj_t* cd_elf_obj_new(int fd, cd_obj_opts_t* opts, cd_error_t* err) {
  cd_elf_obj_t* obj;
  struct stat sbuf;
  char* ptr;

  obj = malloc(sizeof(*obj));
  if (obj == NULL) {
    *err = cd_error_str(kCDErrNoMem, "cd_elf_obj_t");
    goto failed_malloc;
  }

  /* mmap the file */
  if (fstat(fd, &sbuf) != 0) {
    *err = cd_error_num(kCDErrFStat, errno);
    goto failed_fstat;
  }

  *err = cd_obj_internal_init((cd_obj_t*) obj);
  if (!cd_is_ok(*err))
    goto failed_fstat;

  obj->size = sbuf.st_size;
  if (obj->size < sizeof(obj->header)) {
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
  obj->h64 = obj->addr;

  if (memcmp(obj->h64->e_ident, ELFMAG, SELFMAG) != 0) {
    *err = cd_error_str(kCDErrInvalidMagic, obj->h64->e_ident);
    goto failed_magic2;
  }

  /* Only little endian arch is supported, sorry */
  if (obj->h64->e_ident[EI_DATA] != ELFDATA2LSB) {
    *err = cd_error_num(kCDErrBigEndianMagic, obj->h64->e_ident[EI_DATA]);
    goto failed_magic2;
  }

  obj->is_x64 = obj->h64->e_ident[EI_CLASS] == ELFCLASS64;

  if (obj->is_x64) {
    obj->header = *obj->h64;
  } else {
    /* Copy all header data for simplicy of the code below */
    obj->h32 = (Elf32_Ehdr*) obj->h64;
    obj->header.e_type = obj->h32->e_type;
    obj->header.e_machine = obj->h32->e_machine;
    obj->header.e_version = obj->h32->e_version;
    obj->header.e_entry = obj->h32->e_entry;
    obj->header.e_phoff = obj->h32->e_phoff;
    obj->header.e_shoff = obj->h32->e_shoff;
    obj->header.e_flags = obj->h32->e_flags;
    obj->header.e_ehsize = obj->h32->e_ehsize;
    obj->header.e_phentsize = obj->h32->e_phentsize;
    obj->header.e_phnum = obj->h32->e_phnum;
    obj->header.e_shentsize = obj->h32->e_shentsize;
    obj->header.e_shnum = obj->h32->e_shnum;
    obj->header.e_shstrndx = obj->h32->e_shstrndx;
  }

  /* .shstrtab */
  ptr = obj->addr + obj->header.e_shoff +
        obj->header.e_shstrndx * obj->header.e_shentsize;
  if (obj->is_x64) {
    Elf64_Shdr* sect;

    sect = (Elf64_Shdr*) ptr;
    obj->shstrtab = obj->addr + sect->sh_offset;
  } else {
    Elf32_Shdr* sect;

    sect = (Elf32_Shdr*) ptr;
    obj->shstrtab = obj->addr + sect->sh_offset;
  }

  if (cd_elf_obj_is_core(obj)) {
    *err = cd_elf_obj_load_dsos(obj);
    if (!cd_is_ok(*err))
      goto failed_magic2;
  }

  *err = cd_ok();
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


void cd_elf_obj_free(cd_elf_obj_t* obj) {
  munmap(obj->addr, obj->size);
  obj->addr = NULL;

  cd_obj_internal_free((cd_obj_t*) obj);

  free(obj);
}


int cd_elf_obj_is_core(cd_elf_obj_t* obj) {
  return obj->header.e_type == ET_CORE;
}


cd_error_t cd_elf_obj_iterate_segs(cd_elf_obj_t* obj,
                                   cd_obj_iterate_seg_cb cb,
                                   void* arg) {
  cd_error_t err;
  char* ptr;
  cd_segment_t seg;
  int i;

  /* Fill segments */
  ptr = obj->addr + obj->header.e_phoff;
  for (i = 0; i < obj->header.e_phnum; i++, ptr += obj->header.e_phentsize) {
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;

    if (obj->is_x64) {
      Elf64_Phdr* phdr;

      phdr = (Elf64_Phdr*) ptr;
      fileoff = phdr->p_offset;
      vmaddr = phdr->p_vaddr;
      vmsize = phdr->p_memsz;
    } else {
      Elf32_Phdr* phdr;

      phdr = (Elf32_Phdr*) ptr;
      fileoff = phdr->p_offset;
      vmaddr = phdr->p_vaddr;
      vmsize = phdr->p_memsz;
    }

    seg.start = vmaddr;
    seg.end = vmaddr + vmsize;
    seg.fileoff = fileoff;
    seg.ptr = (char*) obj->addr + fileoff;
    seg.sects = 1;

    err = cb((cd_obj_t*) obj, &seg, arg);
    if (!cd_is_ok(err))
      goto fatal;
  }

  return cd_ok();

fatal:
  return err;
}


cd_error_t cd_elf_obj_get_section(cd_elf_obj_t* obj,
                                  const char* name,
                                  char** sect,
                                  uint64_t* size,
                                  uint64_t* addr) {
  char* ptr;
  int i;

  ptr = obj->addr + obj->header.e_shoff;
  for (i = 0; i < obj->header.e_shnum; i++, ptr += obj->header.e_shentsize) {
    char* ent;
    const char* nm;
    uint64_t shsize;
    uint64_t vmaddr;

    if (obj->is_x64) {
      Elf64_Shdr* sect;

      sect = (Elf64_Shdr*) ptr;
      nm = obj->shstrtab + sect->sh_name;
      ent = obj->addr + sect->sh_offset;
      shsize = sect->sh_size;
      vmaddr = sect->sh_addr;
    } else {
      Elf32_Shdr* sect;

      sect = (Elf32_Shdr*) ptr;
      nm = obj->shstrtab + sect->sh_name;
      ent = obj->addr + sect->sh_offset;
      shsize = sect->sh_size;
      vmaddr = sect->sh_addr;
    }

    if (strcmp(nm, name) != 0)
      continue;

    *sect = ent;
    if (size != NULL)
      *size = shsize;
    if (addr != NULL)
      *addr = vmaddr;

    return cd_ok();
  }

  return cd_error_str(kCDErrNotFound, name);
}


cd_error_t cd_elf_obj_iterate_sh(cd_elf_obj_t* obj,
                                 cd_elf_obj_iterate_sh_cb cb,
                                 void* arg) {
  cd_error_t err;
  int i;
  char* ptr;

  ptr = obj->addr + obj->header.e_shoff;
  for (i = 0; i < obj->header.e_shnum; i++, ptr += obj->header.e_shentsize) {
    if (obj->is_x64) {
      Elf64_Shdr* shdr;

      shdr = (Elf64_Shdr*) ptr;
      err = cb(obj, shdr, arg);
      if (!cd_is_ok(err))
        return err;
    } else {
      Elf64_Shdr shdr;
      Elf32_Shdr* shdr32;

      shdr32 = (Elf32_Shdr*) ptr;

      shdr.sh_name = shdr32->sh_name;
      shdr.sh_type = shdr32->sh_type;
      shdr.sh_flags = shdr32->sh_flags;
      shdr.sh_addr = shdr32->sh_addr;
      shdr.sh_offset = shdr32->sh_offset;
      shdr.sh_size = shdr32->sh_size;
      shdr.sh_link = shdr32->sh_link;
      shdr.sh_info = shdr32->sh_info;
      shdr.sh_addralign = shdr32->sh_addralign;
      shdr.sh_entsize = shdr32->sh_entsize;

      err = cb(obj, &shdr, arg);
      if (!cd_is_ok(err))
        return err;
    }
  }

  return cd_ok();
}


typedef struct cd_elf_obj_iterate_syms_s cd_elf_obj_iterate_syms_t;

struct cd_elf_obj_iterate_syms_s {
  char* strtab;
  char* dynstr;
  cd_obj_iterate_sym_cb cb;
  void* arg;
};


cd_error_t cd_elf_obj_iterate_syms_sh(cd_elf_obj_t* obj,
                                      Elf64_Shdr* shdr,
                                      void* arg) {
  cd_error_t err;
  char* ent;
  Elf64_Xword size;
  Elf64_Xword entsize;
  cd_elf_obj_iterate_syms_t* st;
  char* strtab;

  if (shdr->sh_type != SHT_SYMTAB && shdr->sh_type != SHT_DYNSYM)
    return cd_ok();

  st = (cd_elf_obj_iterate_syms_t*) arg;

  if (shdr->sh_type == SHT_SYMTAB)
    strtab = st->strtab;
  else
    strtab = st->dynstr;

  if (strtab == NULL)
    return cd_ok();

  ent = obj->addr + shdr->sh_offset;
  size = shdr->sh_size;
  entsize = shdr->sh_entsize;

  for (; size != 0; size -= entsize, ent += entsize) {
    char* name;
    uint64_t value;
    cd_sym_t sym;

    if (obj->is_x64) {
      Elf64_Sym* sym;

      sym = (Elf64_Sym*) ent;
      name = strtab + sym->st_name;
      value = sym->st_value;
    } else {
      Elf32_Sym* sym;

      sym = (Elf32_Sym*) ent;
      name = strtab + sym->st_name;
      value = sym->st_value;
    }

    sym.name = name;
    sym.nlen = strlen(name);
    sym.value = value;

    err = st->cb((cd_obj_t*) obj, &sym, st->arg);
    if (!cd_is_ok(err))
      return err;
  }

  return cd_ok();
}


cd_error_t cd_elf_obj_iterate_syms(cd_elf_obj_t* obj,
                                   cd_obj_iterate_sym_cb cb,
                                   void* arg) {
  cd_error_t err;
  cd_elf_obj_iterate_syms_t state;

  /* Find string table */
  err = cd_elf_obj_get_section(obj, ".strtab", &state.strtab, NULL, NULL);
  if (!cd_is_ok(err))
    state.strtab = NULL;
  err = cd_elf_obj_get_section(obj, ".dynstr", &state.dynstr, NULL, NULL);
  if (!cd_is_ok(err))
    state.dynstr = NULL;

  state.cb = cb;
  state.arg = arg;
  return cd_elf_obj_iterate_sh(obj, cd_elf_obj_iterate_syms_sh, &state);
}


cd_error_t cd_elf_obj_iterate_notes(cd_elf_obj_t* obj,
                                    cd_elf_obj_iterate_notes_cb cb,
                                    void* arg) {
  cd_error_t err;
  int i;
  char* ptr;

  ptr = obj->addr + obj->header.e_phoff;
  for (i = 0; i < obj->header.e_phnum; i++, ptr += obj->header.e_phentsize) {
    Elf64_Phdr* phdr;
    Elf64_Phdr phdr_st;
    char* ent;
    char* end;

    if (obj->is_x64) {
      phdr = (Elf64_Phdr*) ptr;
    } else {
      Elf32_Phdr* phdr32;

      phdr32 = (Elf32_Phdr*) ptr;
      phdr = &phdr_st;

      phdr->p_type = phdr32->p_type;
      phdr->p_flags = phdr32->p_flags;
      phdr->p_offset = phdr32->p_offset;
      phdr->p_vaddr = phdr32->p_vaddr;
      phdr->p_paddr = phdr32->p_paddr;
      phdr->p_filesz = phdr32->p_filesz;
      phdr->p_memsz = phdr32->p_memsz;
      phdr->p_align = phdr32->p_align;
    }

    if (phdr->p_type != PT_NOTE)
      continue;

    ent = obj->addr + phdr->p_offset;
    end = ent + phdr->p_filesz;

    /* Iterate through actual notes */
    while (ent < end) {
      Elf64_Nhdr* nhdr;
      Elf64_Nhdr nhdr_st;
      char* desc;

      if (obj->is_x64) {
        nhdr = (Elf64_Nhdr*) ent;
        ent += sizeof(*nhdr);
      } else {
        Elf32_Nhdr* nhdr32;

        nhdr32 = (Elf32_Nhdr*) ent;
        nhdr = &nhdr_st;

        nhdr->n_namesz = nhdr32->n_namesz;
        nhdr->n_descsz = nhdr32->n_descsz;
        nhdr->n_type = nhdr32->n_type;

        ent += sizeof(*nhdr32);
      }

      /* Don't forget alignment */
      ent += nhdr->n_namesz;
      if (obj->is_x64) {
        if ((nhdr->n_namesz & 7) != 0)
          ent += 8 - (nhdr->n_namesz & 7);
        desc = ent;
        ent += nhdr->n_descsz;
        if ((nhdr->n_descsz & 7) != 0)
          ent += 8 - (nhdr->n_descsz & 7);
      } else {
        if ((nhdr->n_namesz & 3) != 0)
          ent += 4 - (nhdr->n_namesz & 3);
        desc = ent;
        ent += nhdr->n_descsz;
        if ((nhdr->n_descsz & 3) != 0)
          ent += 4 - (nhdr->n_descsz & 3);
      }

      err = cb(obj, nhdr, desc, arg);
      if (!cd_is_ok(err))
        return err;
    }
  }

  return cd_ok();
}


typedef struct cd_elf_obj_get_thread_s cd_elf_obj_get_thread_t;

struct cd_elf_obj_get_thread_s {
  unsigned int index;
  cd_obj_thread_t* thread;
};


cd_error_t cd_elf_obj_get_thread_iterate(cd_elf_obj_t* obj,
                                         Elf64_Nhdr* nhdr,
                                         char* desc,
                                         void* arg) {
  cd_error_t err;
  cd_elf_obj_get_thread_t* st;
  cd_obj_thread_t* thread;
  unsigned int i;
  cd_segment_t idx;
  cd_segment_t* r;

  if (nhdr->n_type != NT_PRSTATUS)
    return cd_ok();

  st = (cd_elf_obj_get_thread_t*) arg;
  thread = st->thread;

  if (st->index != 0) {
    st->index--;
    return cd_ok();
  }

  if (obj->is_x64) {
    desc += 112;
    thread->regs.count = (nhdr->n_descsz - 112) / sizeof(uint64_t);
    for (i = 0; i < thread->regs.count; i++)
      thread->regs.values[i] = *((uint64_t*) desc + i);

    thread->regs.ip = thread->regs.values[16];
    thread->stack.frame = thread->regs.values[4];
    thread->stack.top = thread->regs.values[19];
  } else {
    desc += 96;
    thread->regs.count = (nhdr->n_descsz - 96) / sizeof(uint32_t);
    for (i = 0; i < thread->regs.count; i++)
      thread->regs.values[i] = *((uint32_t*) desc + i);

    thread->regs.ip = thread->regs.values[12];
    thread->stack.frame = thread->regs.values[5];
    thread->stack.top = thread->regs.values[15];
  }

  /* Find stack start address, end of the segment */
  err = cd_obj_init_segments((cd_obj_t*) obj);
  if (!cd_is_ok(err))
    return err;

  if (obj->segment_count == 0)
    return cd_error(kCDErrNotFound);

  idx.start = thread->stack.top;
  r = cd_splay_find(&obj->seg_splay, &idx);
  if (r == NULL)
    return cd_error(kCDErrNotFound);
  thread->stack.bottom = r->end;

  return cd_error(kCDErrSkip);
}


cd_error_t cd_elf_obj_get_thread(cd_elf_obj_t* obj,
                                 unsigned int index,
                                 cd_obj_thread_t* thread) {
  cd_error_t err;
  cd_elf_obj_get_thread_t state;

  if (!cd_elf_obj_is_core(obj))
    return cd_error_num(kCDErrNotCore, obj->header.e_type);

  state.index = index;
  state.thread = thread;

  err = cd_elf_obj_iterate_notes(obj,
                                 cd_elf_obj_get_thread_iterate,
                                 &state);
  if (err.code == kCDErrSkip)
    return cd_ok();
  if (!cd_is_ok(err))
    return err;

  return cd_error_str(kCDErrNotFound, "thread info");
}


cd_error_t cd_elf_obj_load_dsos_iterate(cd_elf_obj_t* obj,
                                        Elf64_Nhdr* nhdr,
                                        char* desc,
                                        void* arg) {
  struct {
    uint64_t count;
    uint64_t page_size;
  } fhdr;
  uint64_t i;
  char* paths;

  if (nhdr->n_type != NT_FILE)
    return cd_ok();

  /* XXX Check OOBs */
  if (obj->is_x64) {
    fhdr.count = ((uint64_t*) desc)[0];
    fhdr.page_size = ((uint64_t*) desc)[1];
    desc += 16;
    paths = desc + 3 * fhdr.count * 8;
  } else {
    fhdr.count = ((uint32_t*) desc)[0];
    fhdr.page_size = ((uint32_t*) desc)[1];
    desc += 8;
    paths = desc + 3 * fhdr.count * 4;
  }

  for (i = 0; i < fhdr.count; i++) {
    struct {
      uint64_t start;
      uint64_t end;
      uint64_t fileoff;
      char* path;
    } line;
    cd_error_t err;
    cd_obj_t* image;
    cd_obj_opts_t opts;

    if (obj->is_x64) {
      line.start = ((uint64_t*) desc)[0];
      line.end = ((uint64_t*) desc)[1];
      line.fileoff = ((uint64_t*) desc)[2];
      line.path = paths;
      desc += 8 * 3;
    } else {
      line.start = ((uint32_t*) desc)[0];
      line.end = ((uint32_t*) desc)[1];
      line.fileoff = ((uint32_t*) desc)[2];
      line.path = paths;
      desc += 4 * 3;
    }
    paths += strlen(paths) + 1;
    line.fileoff *= fhdr.page_size;

    /* Ignore minor sections */
    if (line.fileoff != 0)
      continue;

    opts.parent = (cd_obj_t*) obj;
    opts.reloc = line.start;

    image = cd_obj_new_ex(cd_elf_obj_method, line.path, &opts, &err);
    if (!cd_is_ok(err))
      continue;
  }

  return cd_ok();
}


cd_error_t cd_elf_obj_load_dsos(cd_elf_obj_t* obj) {
  if (!cd_elf_obj_is_core(obj))
    return cd_error_num(kCDErrNotCore, obj->header.e_type);

  return cd_elf_obj_iterate_notes(obj,
                                  cd_elf_obj_load_dsos_iterate,
                                  NULL);
}


cd_error_t cd_elf_obj_get_dbg(cd_elf_obj_t* obj,
                              void** res,
                              uint64_t* size,
                              uint64_t* vmaddr) {
  return cd_elf_obj_get_section(obj,
                                ".eh_frame",
                                (char**) res,
                                size,
                                vmaddr);
}


cd_obj_method_t cd_elf_obj_method_def = {
  .obj_new = (cd_obj_method_new_t) cd_elf_obj_new,
  .obj_free = (cd_obj_method_free_t) cd_elf_obj_free,
  .obj_is_core = (cd_obj_method_is_core_t) cd_elf_obj_is_core,
  .obj_get_thread = (cd_obj_method_get_thread_t) cd_elf_obj_get_thread,
  .obj_iterate_syms = (cd_obj_method_iterate_syms_t) cd_elf_obj_iterate_syms,
  .obj_iterate_segs = (cd_obj_method_iterate_segs_t) cd_elf_obj_iterate_segs,
  .obj_get_dbg_frame = (cd_obj_method_get_dbg_frame_t) cd_elf_obj_get_dbg
};

cd_obj_method_t* cd_elf_obj_method = &cd_elf_obj_method_def;
