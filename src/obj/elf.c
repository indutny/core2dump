#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <elf.h>

#include "error.h"
#include "obj.h"
#include "common.h"
#include "obj-common.h"


static const int kCDSymtabInitialSize = 16384;


static cd_error_t cd_obj_init_segments(cd_obj_t* obj);
static cd_error_t cd_obj_init_symbols(cd_obj_t* obj);
static cd_error_t cd_obj_get_section(cd_obj_t* obj,
                                     const char* name,
                                     char** sect);


struct cd_obj_s {
  CD_OBJ_COMMON_FIELDS

  Elf64_Ehdr header;
  Elf64_Ehdr* h64;
  Elf32_Ehdr* h32;
  const char* shstrtab;
};



cd_obj_t* cd_obj_new(int fd, cd_error_t* err) {
  cd_obj_t* obj;
  struct stat sbuf;
  char* ptr;

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
  obj->segment_count = -1;

  if (obj->size < sizeof(obj->header)) {
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
  obj->h64 = obj->addr;

  if (memcmp(obj->h64->e_ident, ELFMAG, SELFMAG) != 0) {
    *err = cd_error_str(kCDErrInvalidMagic, obj->h64->e_ident);
    goto failed_magic_check;
  }

  /* Only little endian arch is supported, sorry */
  if (obj->h64->e_ident[EI_DATA] != ELFDATA2LSB) {
    *err = cd_error_num(kCDErrBigEndianMagic, obj->h64->e_ident[EI_DATA]);
    goto failed_magic_check;
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

  if (obj->segment_count != -1) {
    free(obj->segments);
    cd_splay_destroy(&obj->seg_splay);
  }
  free(obj);
}


int cd_obj_is_x64(cd_obj_t* obj) {
  return obj->is_x64;
}


cd_error_t cd_obj_init_segments(cd_obj_t* obj) {
  cd_error_t err;
  cd_segment_t* seg;
  char* ptr;
  int i;

  if (obj->segment_count != -1)
    return cd_ok();

  cd_splay_init(&obj->seg_splay,
                (int (*)(const void*, const void*)) cd_segment_sort);

  /* Allocate segments */
  obj->segment_count = obj->header.e_phnum;
  obj->segments = calloc(obj->segment_count, sizeof(*obj->segments));
  if (obj->segments == NULL)
    return cd_error_str(kCDErrNoMem, "cd_segment_t");

  /* Fill segments */
  seg = obj->segments;
  ptr = obj->addr + obj->header.e_phoff;
  for (i = 0; i < obj->segment_count; i++, ptr += obj->header.e_phentsize) {
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

    seg->start = vmaddr;
    seg->end = vmaddr + vmsize;
    seg->ptr = (char*) obj->addr + fileoff;

    /* Fill the splay tree */
    if (cd_splay_insert(&obj->seg_splay, seg) != 0)
      return cd_error_str(kCDErrNoMem, "seg_splay");

    seg++;
  }

  return cd_ok();

fatal:
  return err;
}


cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res) {
  cd_error_t err;
  cd_segment_t idx;
  cd_segment_t* r;

  if (obj->header.e_type != ET_CORE)
    return cd_error_num(kCDErrNotCore, obj->header.e_type);

  err = cd_obj_init_segments(obj);
  if (!cd_is_ok(err))
    return err;

  if (obj->segment_count == 0)
    return cd_error(kCDErrNotFound);

  idx.start = addr;
  r = cd_splay_find(&obj->seg_splay, &idx);
  if (r == NULL)
    return cd_error(kCDErrNotFound);

  if (addr + size > r->end)
    return cd_error(kCDErrNotFound);

  *res = r->ptr + (addr - r->start);

  return cd_ok();
}


cd_error_t cd_obj_get_section(cd_obj_t* obj, const char* name, char** sect) {
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


cd_error_t cd_obj_init_symbols(cd_obj_t* obj) {
  cd_error_t err;
  char* strtab;
  char* ent;
  Elf64_Xword size;
  Elf64_Xword entsize;

  if (obj->has_syms)
    return cd_ok();

  if (cd_hashmap_init(&obj->syms, kCDSymtabInitialSize, 0) != 0) {
    err = cd_error_str(kCDErrNoMem, "cd_hashmap_t");
    goto fatal;
  }
  obj->has_syms = 1;

  /* Find string table */
  err = cd_obj_get_section(obj, ".strtab", &strtab);
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
      int len;
      uint64_t value;

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

      len = strlen(name);
      if (cd_hashmap_insert(&obj->syms, name, len, (void*) value) != 0) {
        err = cd_error_str(kCDErrNoMem, "cd_hashmap_insert");
        goto fatal;
      }
    }
  });

  return cd_ok();

fatal:
  return err;
}


cd_error_t cd_obj_get_sym(cd_obj_t* obj, const char* sym, uint64_t* addr) {
  cd_error_t err;
  void* res;

  err = cd_obj_init_symbols(obj);
  if (!cd_is_ok(err))
    return err;

  assert(sizeof(void*) == sizeof(*addr));
  res = cd_hashmap_get(&obj->syms, sym, strlen(sym));
  if (res == NULL)
    return cd_error_str(kCDErrNotFound, sym);

  *addr = (uint64_t) res;
  return cd_ok();
}


cd_error_t cd_obj_get_thread(cd_obj_t* obj,
                             unsigned int index,
                             cd_obj_thread_t* thread) {
  cd_error_t err;
  char* ent;
  char* end;

  if (obj->header.e_type != ET_CORE) {
    err = cd_error_num(kCDErrNotCore, obj->header.e_type);
    goto fatal;
  }

  CD_ITERATE_SECTS(ph, Phdr, {
    if (sect->p_type != PT_NOTE)
      continue;

    ent = obj->addr + sect->p_offset;
    end = ent + sect->p_filesz;
  }, {
    while (ent < end) {
      Elf32_Word namesz;
      Elf32_Word descsz;
      Elf32_Word type;
      char* desc;
      cd_segment_t idx;
      cd_segment_t* r;

      if (obj->is_x64) {
        Elf64_Nhdr* nhdr;

        nhdr = (Elf64_Nhdr*) ent;
        namesz = nhdr->n_namesz;
        descsz = nhdr->n_descsz;
        type = nhdr->n_type;
        ent += sizeof(*nhdr);
      } else {
        Elf32_Nhdr* nhdr;

        nhdr = (Elf32_Nhdr*) ent;
        namesz = nhdr->n_namesz;
        descsz = nhdr->n_descsz;
        type = nhdr->n_type;
        ent += sizeof(*nhdr);
      }

      /* Don't forget alignment */
      ent += namesz;
      if ((namesz & 3) != 0)
        ent += 4 - (namesz & 3);
      desc = ent;
      if ((descsz & 3) != 0)
        ent += 4 - (descsz & 3);

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
      err = cd_obj_init_segments(obj);
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
    }
  });

  err = cd_error_str(kCDErrNotFound, "thread info");

fatal:
  return err;
}


#undef CD_ITERATE_SECTS
