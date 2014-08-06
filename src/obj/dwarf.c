#include "obj/dwarf.h"
#include "obj.h"
#include "common.h"
#include "error.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void cd_dwarf_free_cie(cd_dwarf_cie_t* fde);
static void cd_dwarf_free_fde(cd_dwarf_fde_t* fde);
static cd_error_t cd_dwarf_parse_cie(cd_dwarf_cfa_t* cfa,
                                     char** data,
                                     uint64_t size);
static cd_error_t cd_dwarf_parse_fde(cd_dwarf_cie_t* cie,
                                     char** data,
                                     uint64_t size);
static cd_error_t cd_dwarf_leb128(char** data, uint64_t size, uint16_t* res);
static cd_error_t cd_dwarf_sleb128(char** data, uint64_t size, int16_t* res);
static cd_error_t cd_dwarf_read(char** data,
                                uint64_t size,
                                uint8_t enc,
                                int x64,
                                uint64_t* res);


cd_error_t cd_dwarf_parse_cfa(cd_obj_t* obj,
                              void* data,
                              uint64_t size,
                              cd_dwarf_cfa_t** res) {
  cd_dwarf_cfa_t* cfa;
  cd_error_t err;
  void* end;

  cfa = malloc(sizeof(*cfa));
  if (cfa == NULL)
    return cd_error_str(kCDErrNoMem, "cd_dwarf_cfa_t");

  cfa->obj = obj;
  QUEUE_INIT(&cfa->cies);

  /* Parse CIEs one-by-one */
  end = (char*) data + size;
  while (data < end) {
    err = cd_dwarf_parse_cie(cfa, (char**) &data, end - data);
    if (!cd_is_ok(err))
      goto failed_parse_cie;
  }

  *res = cfa;
  return cd_ok();

failed_parse_cie:
  cd_dwarf_free_cfa(cfa);
  return err;
}


void cd_dwarf_free_cfa(cd_dwarf_cfa_t* cfa) {
  QUEUE* q;
  while (!QUEUE_EMPTY(&cfa->cies)) {
    cd_dwarf_cie_t* cie;

    q = QUEUE_HEAD(&cfa->cies);
    QUEUE_REMOVE(q);
    cie = container_of(q, cd_dwarf_cie_t, member);

    cd_dwarf_free_cie(cie);
  }
  free(cfa);
}


void cd_dwarf_free_cie(cd_dwarf_cie_t* cie) {
  QUEUE* q;
  while (!QUEUE_EMPTY(&cie->fdes)) {
    cd_dwarf_fde_t* fde;

    q = QUEUE_HEAD(&cie->fdes);
    QUEUE_REMOVE(q);
    fde = container_of(q, cd_dwarf_fde_t, member);

    cd_dwarf_free_fde(fde);
  }
  free(cie);
}


void cd_dwarf_free_fde(cd_dwarf_fde_t* fde) {
  free(fde);
}


cd_error_t cd_dwarf_leb128(char** data, uint64_t size, uint16_t* res) {
  uint8_t fb;
  uint8_t sb;

  if (size < 1)
    return cd_error_str(kCDErrDwarfOOB, "LEB128 - fb");

  fb = *(uint8_t*) *data;
  (*data)++;
  if ((fb & 0x80) == 0) {
    *res = (uint16_t) fb;
    return cd_ok();
  }

  fb &= 0x7f;
  if (size < 2)
    return cd_error_str(kCDErrDwarfOOB, "LEB128 - fb");

  sb = *(uint8_t*) *data;
  (*data)++;

  *res = (uint16_t) sb * 0x80 | fb;
  return cd_ok();
}


cd_error_t cd_dwarf_sleb128(char** data, uint64_t size, int16_t* res) {
  uint8_t fb;
  uint8_t sb;

  if (size < 1)
    return cd_error_str(kCDErrDwarfOOB, "LEB128 - fb");

  fb = *(uint8_t*) *data;
  (*data)++;
  if ((fb & 0x80) == 0) {
    if ((fb & 0x40) == 0)
      *res = (uint16_t) fb;
    else
      *res = (int16_t) fb - 128;
    return cd_ok();
  }

  fb &= 0x7f;
  if (size < 2)
    return cd_error_str(kCDErrDwarfOOB, "LEB128 - fb");

  sb = *(uint8_t*) *data;
  (*data)++;

  if ((fb & 0x40) == 0)
    *res = (int16_t) sb * 0x80 | fb;
  else
    *res = ((int16_t) sb * 0x80 | fb) - 16384;
  return cd_ok();
}


cd_error_t cd_dwarf_read(char** data,
                         uint64_t size,
                         uint8_t enc,
                         int x64,
                         uint64_t* res) {
  switch (enc & kCDDwarfEncodeMask) {
    case kCDDwarfAbsPtr:
      if (x64) {
        if (size < 8)
          return cd_error_str(kCDErrDwarfOOB, "dwarf_read x64 ptr");
        *res = *(uint64_t*) *data;
        (*data) += 8;
      } else {
        if (size < 4)
          return cd_error_str(kCDErrDwarfOOB, "dwarf_read x32 ptr");
        *res = *(uint32_t*) *data;
        (*data) += 4;
      }
      break;
    default:
      abort();
  }
  return cd_ok();
}


cd_error_t cd_dwarf_parse_cie(cd_dwarf_cfa_t* cfa,
                              char** data,
                              uint64_t size) {
  cd_error_t err;
  cd_dwarf_cie_t* cie;
  char* end;
  int x64;

  cie = malloc(sizeof(*cie));
  if (cie == NULL)
    return cd_error_str(kCDErrNoMem, "cd_dwarf_cie_t");

  cie->cfa = cfa;

  QUEUE_INIT(&cie->fdes);
  /* TODO(indutny): bounds checks */

  cie->len = *(uint32_t*) *data;
  (*data) += 4;
  size -= 4;
  x64 = cie->len == 0xffffffff;
  if (x64) {
    cie->len = *(uint64_t*) *data;
    (*data) += 8;
    size -= 8;
  }
  if (cie->len > size) {
    err = cd_error_str(kCDErrDwarfOOB, "CIE length");
    goto fatal;
  }

  size -= cie->len;
  end = *data + cie->len;

  if (x64) {
    cie->id = *(uint64_t*) *data;
    (*data) += 8;
  } else {
    cie->id = *(uint32_t*) *data;
    (*data) += 4;
  }

  /* Unexpected FDE */
  if (cie->id != 0) {
    err = cd_error_str(kCDErrDwarfOOB, "Unexpected FDE");
    goto fatal;
  }

  cie->version = *(uint8_t*) *data;
  (*data)++;

  cie->augment = *data;
  (*data) += strlen(cie->augment) + 1;

  err = cd_dwarf_leb128(data, end - *data, &cie->code_align);
  if (!cd_is_ok(err))
    goto fatal;
  err = cd_dwarf_sleb128(data, end - *data, &cie->data_align);
  if (!cd_is_ok(err))
    goto fatal;
  err = cd_dwarf_leb128(data, end - *data, &cie->ret_reg);
  if (!cd_is_ok(err))
    goto fatal;

  cie->fde_enc = 0;
  if (cie->augment[0] == 'z') {
    char* pos;

    err = cd_dwarf_leb128(data, end - *data, &cie->aug_len);
    if (!cd_is_ok(err))
      goto fatal;

    cie->aug_data = *data;
    (*data) += cie->aug_len;

    pos = strchr(cie->augment, 'R');
    if (pos != NULL) {
      int off;

      off = pos - cie->augment - 1;
      if (off < cie->aug_len)
        cie->fde_enc = cie->aug_data[off];
    }
  } else {
    cie->aug_len = 0;
    cie->aug_data = NULL;
  }

  /* Skip the rest */
  *data = end;

  do {
    char* start;

    start = *data;
    err = cd_dwarf_parse_fde(cie, data, size);
    if (err.code == kCDErrSkip) {
      /* Revert lookup */
      *data = start;
    } else {
      size -= *data - start;
    }
  } while (cd_is_ok(err));
  if (!cd_is_ok(err) && err.code != kCDErrSkip)
    goto fatal;

  QUEUE_INSERT_TAIL(&cfa->cies, &cie->member);

  return cd_ok();

fatal:
  cd_dwarf_free_cie(cie);
  return err;
}


cd_error_t cd_dwarf_parse_fde(cd_dwarf_cie_t* cie, char** data, uint64_t size) {
  cd_error_t err;
  cd_dwarf_fde_t* fde;
  char* end;
  int x64;

  fde = malloc(sizeof(*fde));
  if (fde == NULL)
    return cd_error_str(kCDErrNoMem, "cd_dwarf_fde_t");

  fde->cie = cie;

  /* TODO(indutny): bounds checks */

  fde->len = *(uint32_t*) *data;
  (*data) += 4;
  size -= 4;
  x64 = fde->len == 0xffffffff;
  if (x64) {
    fde->len = *(uint64_t*) *data;
    (*data) += 8;
    size -= 8;
  }
  if (fde->len > size) {
    err = cd_error_str(kCDErrDwarfOOB, "FDE length");
    goto fatal;
  }

  size -= fde->len;
  end = *data + fde->len;

  if (x64) {
    fde->cie_off = *(uint64_t*) *data;
    (*data) += 8;
  } else {
    fde->cie_off = *(uint32_t*) *data;
    (*data) += 4;
  }

  /* Unexpected CIE */
  if (fde->cie_off == 0) {
    err = cd_error_str(kCDErrSkip, "Unexpected CIE");
    goto fatal;
  }

  err = cd_dwarf_read(data,
                      end - *data,
                      cie->fde_enc,
                      cie->cfa->obj->is_x64,
                      &fde->init_loc);
  if (!cd_is_ok(err))
    goto fatal;
  err = cd_dwarf_read(data,
                      end - *data,
                      cie->fde_enc,
                      cie->cfa->obj->is_x64,
                      &fde->range);
  if (!cd_is_ok(err))
    goto fatal;

  if (cie->augment[0] == 'z') {
    err = cd_dwarf_leb128(data, end - *data, &fde->aug_len);
    if (!cd_is_ok(err))
      goto fatal;

    fde->aug_data = *data;
    (*data) += fde->aug_len;
  } else {
    fde->aug_len = 0;
    fde->aug_data = NULL;
  }

  fde->instr_len = end - *data;
  fde->instrs = *data;

  /* Skip the rest */
  *data = end;

  QUEUE_INSERT_TAIL(&cie->fdes, &fde->member);
  return cd_ok();

fatal:
  cd_dwarf_free_fde(fde);
  return err;
}
