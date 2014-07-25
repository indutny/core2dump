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

static cd_error_t cd_elf_obj_get_section(cd_elf_obj_t* obj,
                                         const char* name,
                                         char** sect);
static cd_error_t cd_elf_obj_load_dsos(cd_elf_obj_t* obj);


struct cd_elf_obj_s {
  CD_OBJ_INTERNAL_FIELDS

  Elf64_Ehdr header;
  Elf64_Ehdr* h64;
  Elf32_Ehdr* h32;
  const char* shstrtab;
};


cd_elf_obj_t* cd_elf_obj_new(int fd, void* opts, cd_error_t* err) {
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
                                  char** sect) {
  char* ptr;
  int i;

  ptr = obj->addr + obj->header.e_shoff;
  for (i = 0; i < obj->header.e_shnum; i++, ptr += obj->header.e_shentsize) {
    char* ent;
    const char* nm;

    if (obj->is_x64) {
      Elf64_Shdr* sect;

      sect = (Elf64_Shdr*) ptr;
      nm = obj->shstrtab + sect->sh_name;
      ent = obj->addr + sect->sh_offset;
    } else {
      Elf32_Shdr* sect;

      sect = (Elf32_Shdr*) ptr;
      nm = obj->shstrtab + sect->sh_name;
      ent = obj->addr + sect->sh_offset;
    }

    if (strcmp(nm, name) != 0)
      continue;

    *sect = ent;

    return cd_ok();
  }

  return cd_error_str(kCDErrNotFound, name);
}


#define CD_ITERATE_SECTS(prefix, postfix, spec, shared)                       \
    do {                                                                      \
      int i;                                                                  \
      char* ptr;                                                              \
      ptr = obj->addr + obj->header.e_##prefix##off;                          \
      for (i = 0;                                                             \
           i < obj->header.e_##prefix##num;                                   \
           i++, ptr += obj->header.e_##prefix##entsize) {                     \
        if (obj->is_x64) {                                                    \
          Elf64_##postfix* sect;                                              \
          sect = (Elf64_##postfix*) ptr;                                      \
          spec                                                                \
        } else {                                                              \
          Elf32_##postfix* sect;                                              \
          sect = (Elf32_##postfix*) ptr;                                      \
          spec                                                                \
        }                                                                     \
        shared                                                                \
      }                                                                       \
    } while (0)                                                               \


cd_error_t cd_elf_obj_iterate_syms(cd_elf_obj_t* obj,
                                   cd_obj_iterate_sym_cb cb,
                                   void* arg) {
  cd_error_t err;
  char* strtab;
  char* ent;
  Elf64_Xword size;
  Elf64_Xword entsize;

  /* Find string table */
  err = cd_elf_obj_get_section(obj, ".strtab", &strtab);
  if (!cd_is_ok(err))
    goto fatal;

  CD_ITERATE_SECTS(sh, Shdr, {
    if (sect->sh_type != SHT_SYMTAB)
      continue;

    ent = obj->addr + sect->sh_offset;
    size = sect->sh_size;
    entsize = sect->sh_entsize;
  }, {
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

      err = cb((cd_obj_t*) obj, &sym, arg);
      if (!cd_is_ok(err))
        goto fatal;
    }
  });

  return cd_ok();

fatal:
  return err;
}


#define CD_ITERATE_NOTES(BODY)                                                \
    do {                                                                      \
      char* ent;                                                              \
      char* end;                                                              \
      CD_ITERATE_SECTS(ph, Phdr, {                                            \
        if (sect->p_type != PT_NOTE)                                          \
          continue;                                                           \
                                                                              \
        ent = obj->addr + sect->p_offset;                                     \
        end = ent + sect->p_filesz;                                           \
      }, {                                                                    \
        while (ent < end) {                                                   \
          Elf32_Word namesz;                                                  \
          Elf32_Word descsz;                                                  \
          Elf32_Word type;                                                    \
          char* desc;                                                         \
          cd_segment_t idx;                                                   \
          cd_segment_t* r;                                                    \
                                                                              \
          if (obj->is_x64) {                                                  \
            Elf64_Nhdr* nhdr;                                                 \
                                                                              \
            nhdr = (Elf64_Nhdr*) ent;                                         \
            namesz = nhdr->n_namesz;                                          \
            descsz = nhdr->n_descsz;                                          \
            type = nhdr->n_type;                                              \
            ent += sizeof(*nhdr);                                             \
          } else {                                                            \
            Elf32_Nhdr* nhdr;                                                 \
                                                                              \
            nhdr = (Elf32_Nhdr*) ent;                                         \
            namesz = nhdr->n_namesz;                                          \
            descsz = nhdr->n_descsz;                                          \
            type = nhdr->n_type;                                              \
            ent += sizeof(*nhdr);                                             \
          }                                                                   \
                                                                              \
          /* Don't forget alignment */                                        \
          ent += namesz;                                                      \
          if (obj->is_x64) {                                                  \
            if ((namesz & 7) != 0)                                            \
              ent += 8 - (namesz & 7);                                        \
            desc = ent;                                                       \
            if ((descsz & 7) != 0)                                            \
              ent += 8 - (descsz & 7);                                        \
          } else {                                                            \
            if ((namesz & 3) != 0)                                            \
              ent += 4 - (namesz & 3);                                        \
            desc = ent;                                                       \
            if ((descsz & 3) != 0)                                            \
              ent += 4 - (descsz & 3);                                        \
          }                                                                   \
                                                                              \
          if (1) BODY                                                         \
        }                                                                     \
      });                                                                     \
    } while (0)                                                               \


cd_error_t cd_elf_obj_get_thread(cd_elf_obj_t* obj,
                                 unsigned int index,
                                 cd_obj_thread_t* thread) {
  cd_error_t err;

  if (!cd_elf_obj_is_core(obj)) {
    err = cd_error_num(kCDErrNotCore, obj->header.e_type);
    goto fatal;
  }

  CD_ITERATE_NOTES({
    if (type != NT_PRSTATUS)
      continue;

    if (index != 0) {
      index--;
      continue;
    }

    if (obj->is_x64) {
      unsigned int i;

      desc += 112;
      descsz -= 112;
      thread->regs.count = descsz / sizeof(uint64_t);
      for (i = 0; i < thread->regs.count; i++)
        thread->regs.values[i] = *((uint64_t*) desc + i);

      thread->regs.ip = thread->regs.values[16];
      thread->stack.frame = thread->regs.values[4];
      thread->stack.top = thread->regs.values[19];
    } else {
      unsigned int i;

      desc += 96;
      descsz -= 96;
      thread->regs.count = descsz / sizeof(uint32_t);
      for (i = 0; i < thread->regs.count; i++)
        thread->regs.values[i] = *((uint32_t*) desc + i);

      thread->regs.ip = thread->regs.values[12];
      thread->stack.frame = thread->regs.values[5];
      thread->stack.top = thread->regs.values[15];
    }

    /* Find stack start address, end of the segment */
    err = cd_obj_init_segments((cd_obj_t*) obj);
    if (!cd_is_ok(err))
      goto fatal;

    if (obj->segment_count == 0) {
      err = cd_error(kCDErrNotFound);
      goto fatal;
    }

    idx.start = thread->stack.top;
    r = cd_splay_find(&obj->seg_splay, &idx);
    if (r == NULL) {
      err = cd_error(kCDErrNotFound);
      goto fatal;
    }
    thread->stack.bottom = r->end;

    return cd_ok();
  });

  err = cd_error_str(kCDErrNotFound, "thread info");

fatal:
  return err;
}


cd_error_t cd_elf_obj_load_dsos(cd_elf_obj_t* obj) {
  cd_error_t err;
  char* ent;
  char* end;

  if (!cd_elf_obj_is_core(obj)) {
    err = cd_error_num(kCDErrNotCore, obj->header.e_type);
    goto fatal;
  }

  CD_ITERATE_NOTES({
    if (type != NT_FILE)
      continue;

  });

  err = cd_ok();

fatal:
  return err;
}


cd_obj_method_t cd_elf_obj_method_def = {
  .obj_new = (cd_obj_method_new_t) cd_elf_obj_new,
  .obj_free = (cd_obj_method_free_t) cd_elf_obj_free,
  .obj_is_core = (cd_obj_method_is_core_t) cd_elf_obj_is_core,
  .obj_get_thread = (cd_obj_method_get_thread_t) cd_elf_obj_get_thread,
  .obj_iterate_syms = (cd_obj_method_iterate_syms_t) cd_elf_obj_iterate_syms,
  .obj_iterate_segs = (cd_obj_method_iterate_segs_t) cd_elf_obj_iterate_segs
};

cd_obj_method_t* cd_elf_obj_method = &cd_elf_obj_method_def;


#undef CD_ITERATE_SECTS
#undef CD_ITERATE_NOTES
