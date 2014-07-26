#include "obj/dwarf.h"
#include "obj.h"
#include "common.h"
#include "error.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void cd_dwarf_free_cie(cd_dwarf_cie_t* fde);
static void cd_dwarf_free_fde(cd_dwarf_fde_t* fde);
static cd_error_t cd_dwarf_parse_cie_aug(cd_dwarf_cie_t* cie,
                                         char** data,
                                         uint64_t size);
static cd_error_t cd_dwarf_parse_cie(cd_dwarf_cfa_t* cfa,
                                     char** data,
                                     uint64_t size);
static cd_error_t cd_dwarf_parse_fde(cd_dwarf_cfa_t* cfa,
                                     char** data,
                                     uint64_t size);
static cd_error_t cd_dwarf_leb128(char** data, uint64_t size, uint64_t* res);
static cd_error_t cd_dwarf_sleb128(char** data, uint64_t size, int64_t* res);
static cd_error_t cd_dwarf_read(char** data,
                                uint64_t size,
                                uint8_t enc,
                                int x64,
                                uint64_t* res);
cd_error_t cd_dwarf_run(cd_dwarf_cie_t* cie,
                        char* instrs,
                        uint64_t instr_len,
                        uint64_t rip,
                        cd_dwarf_state_t* prev,
                        cd_dwarf_state_t* state);
static int cd_dwarf_sort_cie(cd_dwarf_cie_t* a, cd_dwarf_cie_t* b);
static int cd_dwarf_sort_fde(cd_dwarf_fde_t* a, cd_dwarf_fde_t* b);
static cd_error_t cd_dwarf_treg(cd_obj_thread_t* thread,
                                cd_dwarf_reg_t reg,
                                uint64_t** res);
static uint64_t cd_dwarf_reg_off(cd_dwarf_reg_t reg, int x64);
static cd_error_t cd_dwarf_oreg(cd_obj_thread_t* thread,
                                int x64,
                                uint64_t reg,
                                uint64_t* res);
cd_error_t cd_dwarf_load(cd_dwarf_state_t* state,
                         cd_obj_thread_t* othread,
                         cd_obj_thread_t* nthread,
                         cd_dwarf_reg_t reg,
                         int x64,
                         char* stack,
                         uint64_t stack_size);


cd_error_t cd_dwarf_parse_cfa(cd_obj_t* obj,
                              uint64_t sect_addr,
                              void* data,
                              uint64_t size,
                              cd_dwarf_cfa_t** res) {
  cd_dwarf_cfa_t* cfa;
  cd_error_t err;
  void* end;

  cfa = malloc(sizeof(*cfa));
  if (cfa == NULL)
    return cd_error_str(kCDErrNoMem, "cd_dwarf_cfa_t");

  QUEUE_INIT(&cfa->cies);
  cfa->obj = obj;
  cfa->start = (char*) data;
  cfa->sect_addr = sect_addr;

  cd_splay_init(&cfa->cie_splay, (cd_splay_sort_cb) cd_dwarf_sort_cie);
  cd_splay_init(&cfa->fde_splay, (cd_splay_sort_cb) cd_dwarf_sort_fde);

  /* Parse CIEs one-by-one */
  end = (char*) data + size;
  while (data < end) {
    err = cd_dwarf_parse_cie(cfa, (char**) &data, end - data);
    if (!cd_is_ok(err) && err.code != kCDErrSkip)
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


cd_error_t cd_dwarf_leb128(char** data, uint64_t size, uint64_t* res) {
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


cd_error_t cd_dwarf_sleb128(char** data, uint64_t size, int64_t* res) {
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
  switch (enc & kCDDwarfEncEncodeMask) {
    case kCDDwarfEncAbsPtr:
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
    case kCDDwarfEncULeb128:
      return cd_dwarf_leb128(data, size, res);
    case kCDDwarfEncSLeb128:
      return cd_dwarf_sleb128(data, size, (int64_t*) res);

    /* XXX Bounds checks */
    case kCDDwarfEncUData2:
      *res = *(uint16_t*) *data;
      (*data) += 2;
      break;
    case kCDDwarfEncSData2:
      *res = *(int16_t*) *data;
      (*data) += 2;
      break;
    case kCDDwarfEncUData4:
      *res = *(uint32_t*) *data;
      (*data) += 4;
      break;
    case kCDDwarfEncSData4:
      *res = *(int32_t*) *data;
      (*data) += 4;
      break;
    case kCDDwarfEncUData8:
      *res = *(uint64_t*) *data;
      (*data) += 8;
      break;
    case kCDDwarfEncSData8:
      *res = *(int64_t*) *data;
      (*data) += 8;
      break;
    default:
      abort();
  }
  return cd_ok();
}


cd_error_t cd_dwarf_parse_cie_aug(cd_dwarf_cie_t* cie,
                                  char** data,
                                  uint64_t size) {
  cd_error_t err;
  char* end;
  const char* ptr;
  char* val;

  end = *data + size;

  cie->fde_enc = 0;
  if (cie->augment[0] != 'z') {
    cie->aug_len = 0;
    cie->aug_data = NULL;
    return cd_ok();
  }

  err = cd_dwarf_leb128(data, end - *data, &cie->aug_len);
  if (!cd_is_ok(err))
    return err;

  cie->aug_data = *data;
  val = *data;
  (*data) += cie->aug_len;
  end = *data;

  /* XXX Bounds checks! */
  ptr = cie->augment + 1;
  for (; *ptr != '\0'; ptr++) {
    uint8_t enc;
    uint64_t tmp;

    switch (*ptr) {
      case 'L':
        val++;
        break;
      case 'P':
        enc =  *(uint8_t*) val++;
        err = cd_dwarf_read(&val,
                            end - val,
                            enc,
                            cie->cfa->obj->is_x64,
                            &tmp);
        if (!cd_is_ok(err))
          return err;
        break;
      case 'R':
        cie->fde_enc = *(uint8_t*) val++;
        break;
      case 'S':
        break;
      default:
        return cd_error_str(kCDErrDwarfInvalidAugment, cie->augment);
    }
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
  cie->start = *data;

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

  /* Zero terminator CIE */
  if (cie->len == 0) {
    err = cd_error_str(kCDErrSkip, "Zero terminator");
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

  err = cd_dwarf_parse_cie_aug(cie, data, end - *data);
  if (!cd_is_ok(err))
    goto fatal;

  cie->instr_len = end - *data;
  cie->instrs = *data;

  /* Skip the rest */
  *data = end;

  /* Insert CIE before we start parsing FDEs */
  if (cd_splay_insert(&cfa->cie_splay, cie) != 0) {
    err = cd_error_str(kCDErrNoMem, "cie_splay insert");
    goto fatal;
  }

  do {
    char* start;

    start = *data;
    err = cd_dwarf_parse_fde(cfa, data, size);
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


cd_error_t cd_dwarf_parse_fde(cd_dwarf_cfa_t* cfa, char** data, uint64_t size) {
  cd_error_t err;
  cd_dwarf_fde_t* fde;
  cd_dwarf_cie_t* cie;
  cd_dwarf_cie_t idx;
  char* end;
  char* cie_ptr;
  int x64;
  uint64_t ip;

  fde = malloc(sizeof(*fde));
  if (fde == NULL)
    return cd_error_str(kCDErrNoMem, "cd_dwarf_fde_t");

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

  /* Zero terminator FDE */
  if (fde->len == 0) {
    err = cd_error_str(kCDErrSkip, "Unexpected CIE");
    goto fatal;
  }

  size -= fde->len;
  end = *data + fde->len;
  cie_ptr = *data;

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

  /* Find existing CIE */
  cie_ptr -= fde->cie_off;
  idx.start = cie_ptr;
  cie = cd_splay_find(&cfa->cie_splay, &idx);
  if (cie == NULL || cie->start != cie_ptr)
    return cd_error_str(kCDErrNotFound, "cd_dwarf_cie_t in splay");

  fde->cie = cie;

  ip = cfa->sect_addr + (*data - cfa->start);
  err = cd_dwarf_read(data,
                      end - *data,
                      cie->fde_enc,
                      cfa->obj->is_x64,
                      &fde->init_loc);
  if (!cd_is_ok(err))
    goto fatal;
  err = cd_dwarf_read(data,
                      end - *data,
                      cie->fde_enc,
                      cfa->obj->is_x64,
                      &fde->range);
  if (!cd_is_ok(err))
    goto fatal;

  if ((cie->fde_enc & kCDDwarfEncAppMask) == kCDDwarfEncPCrel) {
    switch ((cie->fde_enc & kCDDwarfEncEncodeMask)) {
      case kCDDwarfEncULeb128:
      case kCDDwarfEncUData2:
      case kCDDwarfEncUData4:
      case kCDDwarfEncUData8:
        fde->init_loc += ip;
        break;
      case kCDDwarfEncSLeb128:
      case kCDDwarfEncSData2:
      case kCDDwarfEncSData4:
      case kCDDwarfEncSData8:
      case kCDDwarfEncAbsPtr:
        fde->init_loc = ip + (int64_t) fde->init_loc;
        break;
      default:
        break;
    }
  }

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

  if (cd_splay_insert(&cfa->fde_splay, fde) != 0) {
    err = cd_error_str(kCDErrNoMem, "fde_splay insert");
    goto fatal;
  }

  QUEUE_INSERT_TAIL(&cie->fdes, &fde->member);
  return cd_ok();

fatal:
  cd_dwarf_free_fde(fde);
  return err;
}


int cd_dwarf_sort_cie(cd_dwarf_cie_t* a, cd_dwarf_cie_t* b) {
  uintptr_t ap;
  uintptr_t bp;

  ap = (uintptr_t) a->start;
  bp = (uintptr_t) b->start;
  return ap > bp ? 1 : ap < bp ? -1 : 0;
}


int cd_dwarf_sort_fde(cd_dwarf_fde_t* a, cd_dwarf_fde_t* b) {
  return a->init_loc > b->init_loc ? 1 : a->init_loc < b->init_loc ? -1 : 0;
}


cd_error_t cd_dwarf_get_fde(cd_dwarf_cfa_t* cfa,
                            uint64_t addr,
                            cd_dwarf_fde_t** res) {
  cd_dwarf_fde_t idx;

  idx.init_loc = addr;
  *res = cd_splay_find(&cfa->fde_splay, &idx);
  if (*res == NULL)
    return cd_error_str(kCDErrNotFound, "cd_dwarf_fde_t in splay");

  return cd_ok();
}


cd_error_t cd_dwarf_run(cd_dwarf_cie_t* cie,
                        char* instrs,
                        uint64_t instr_len,
                        uint64_t rip,
                        cd_dwarf_state_t* prev,
                        cd_dwarf_state_t* state) {
  cd_error_t err;
  char* end;
  char* ptr;
  int x64;
  int ptr_size;
  int64_t data_align;
  cd_dwarf_state_t* history;
  int history_size;
  int history_off;

  ptr = instrs;
  end = ptr + instr_len;
  x64 = cie->cfa->obj->is_x64;
  ptr_size = x64 ? 8 : 4;
  data_align = cie->data_align;

  /* For remember/restore */
  history = NULL;
  history_size = 0;
  history_off = 0;

  while (ptr < end) {
    cd_dwarf_cfa_instr_t opcode;
    uint8_t hi;
    uint8_t lo;
    uint64_t arg0;
    uint64_t arg1;

    opcode = *(uint8_t*) ptr++;
    hi = opcode & kCDDwarfCFAHighMask;
    lo = opcode & kCDDwarfCFALowMask;

    arg0 = 0;
    arg1 = 0;

    if (hi != 0)
      opcode = hi;
    else
      opcode = lo;

    /* Read opcode arguments */
    switch (opcode) {
      /* Inline arg */
      case kCDDwarfCFARestore:
      case kCDDwarfCFAAdvanceLoc:
        arg0 = (uint64_t) lo;
        break;
      case kCDDwarfCFAOffset:
        arg0 = (uint64_t) lo;
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg1);
        break;
      /* No args */
      case kCDDwarfCFANop:
      case kCDDwarfCFARememberState:
      case kCDDwarfCFARestoreState:
        break;
      /* Address */
      case kCDDwarfCFASetLoc:
        err = cd_dwarf_read(&ptr, end - ptr, kCDDwarfEncAbsPtr, x64, &arg0);
        break;
      case kCDDwarfCFAAdvanceLoc1:
        if (ptr + 1 > end)
          err = cd_error_str(kCDErrDwarfOOB, "advance_loc1");
        else
          arg0 = *(uint8_t*) ptr++;
        break;
      case kCDDwarfCFAAdvanceLoc2:
        err = cd_dwarf_read(&ptr, end - ptr, kCDDwarfEncUData2, x64, &arg0);
        break;
      case kCDDwarfCFAAdvanceLoc4:
        err = cd_dwarf_read(&ptr, end - ptr, kCDDwarfEncUData4, x64, &arg0);
        break;
      case kCDDwarfCFAOffsetExtended:
      case kCDDwarfCFARegister:
      case kCDDwarfCFADefCFA:
      case kCDDwarfCFAValOffset:
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg0);
        if (!cd_is_ok(err))
          break;
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg1);
        break;
      case kCDDwarfCFARestoreExtended:
      case kCDDwarfCFAUndefined:
      case kCDDwarfCFASameValue:
      case kCDDwarfCFADefCFARegister:
      case kCDDwarfCFADefCFAOffset:
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg0);
        break;
      case kCDDwarfCFADefCFAExpression:
        /* XXX Block?! */
        abort();
        break;
      case kCDDwarfCFAExpression:
      case kCDDwarfCFAValExpression:
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg0);
        /* XXX arg1 - Block?! */
        abort();
        break;
      case kCDDwarfCFAOffsetExtendedSF:
      case kCDDwarfCFADefCFASF:
      case kCDDwarfCFAValOffsetSF:
        err = cd_dwarf_leb128(&ptr, end - ptr, &arg0);
        if (!cd_is_ok(err))
          break;
        err = cd_dwarf_sleb128(&ptr, end - ptr, (int64_t*) &arg1);
        break;
        break;
      case kCDDwarfCFADefCFAOffsetSF:
        err = cd_dwarf_sleb128(&ptr, end - ptr, (int64_t*) &arg0);
        break;
      default:
        err = cd_error_num(kCDErrDwarfInstruction, opcode);
        break;
    }
    if (!cd_is_ok(err))
      goto fatal;

    /* Execute opcode */
    switch (opcode) {
      case kCDDwarfCFAAdvanceLoc:
      case kCDDwarfCFAAdvanceLoc1:
      case kCDDwarfCFAAdvanceLoc2:
      case kCDDwarfCFAAdvanceLoc4:
        state->loc += arg0;
        break;
      case kCDDwarfCFADefCFA:
        state->cfa.type = kCDDwarfLocMem;
        state->cfa.reg = arg0;
        state->cfa.off = arg1;
        break;
      case kCDDwarfCFADefCFASF:
        state->cfa.type = kCDDwarfLocMem;
        state->cfa.reg = arg0;
        state->cfa.off = (int64_t) arg1 * ptr_size;
        break;
      case kCDDwarfCFADefCFARegister:
        state->cfa.type = kCDDwarfLocMem;
        state->cfa.reg = arg0;
        break;
      case kCDDwarfCFADefCFAOffset:
        state->cfa.type = kCDDwarfLocMem;
        state->cfa.off = arg0;
        break;
      case kCDDwarfCFADefCFAOffsetSF:
        state->cfa.type = kCDDwarfLocMem;
        state->cfa.off = (int64_t) arg0 * ptr_size;
        break;

      /* Register stuff */
      case kCDDwarfCFAUndefined:
        state->regs[arg0].type = kCDDwarfLocUndefined;
        break;
      case kCDDwarfCFASameValue:
        state->regs[arg0].type = kCDDwarfLocReg;
        state->regs[arg0].reg = arg0;
        break;
      case kCDDwarfCFAOffset:
      case kCDDwarfCFAOffsetExtended:
        state->regs[arg0].type = kCDDwarfLocMem;
        state->regs[arg0].off = arg1 * data_align;
        break;
      case kCDDwarfCFAOffsetExtendedSF:
        state->regs[arg0].type = kCDDwarfLocMem;
        state->regs[arg0].off = (int64_t) arg1 * ptr_size * data_align;
        break;
      case kCDDwarfCFAValOffset:
        state->regs[arg0].type = kCDDwarfLocValue;
        state->regs[arg0].off = arg1 * data_align;
        break;
      case kCDDwarfCFAValOffsetSF:
        state->regs[arg0].type = kCDDwarfLocValue;
        state->regs[arg0].off = (int64_t) arg1 * ptr_size * data_align;
        break;
      case kCDDwarfCFARegister:
        state->regs[arg0].type = kCDDwarfLocReg;
        state->regs[arg0].reg = arg1;
        break;
      case kCDDwarfCFARestore:
      case kCDDwarfCFARestoreExtended:
        state->regs[arg0] = prev->regs[arg0];
        break;
      case kCDDwarfCFARememberState:
        /* Grow history */
        if (history_off == history_size) {
          cd_dwarf_state_t* tmp;

          history_size += 4;
          tmp = realloc(history, sizeof(*tmp) * history_size);
          if (tmp == NULL) {
            err = cd_error_str(kCDErrNoMem, "dwarf state history");
            goto fatal;
          }

          history = tmp;
        }

        history[history_off++] = *state;
        break;
      case kCDDwarfCFARestoreState:
        if (history_off == 0) {
          err = cd_error_str(kCDErrDwarfOOB, "dwarf history is empty");
          goto fatal;
        }
        *state = history[--history_off];
        break;
      default:
        break;
    }

    /* Know where to stop */
    if (state->loc > rip)
      break;
  }

  free(history);
  return cd_ok();

fatal:
  free(history);
  return err;
}


cd_error_t cd_dwarf_treg(cd_obj_thread_t* thread,
                         cd_dwarf_reg_t reg,
                         uint64_t** res) {
  switch (reg) {
    case kCDDwarfRegFrame: *res = &thread->stack.frame; break;
    case kCDDwarfRegStack: *res = &thread->stack.top; break;
    case kCDDwarfRegIP: *res = &thread->regs.ip; break;
  }
  return cd_ok();
}


uint64_t cd_dwarf_reg_off(cd_dwarf_reg_t reg, int x64) {
  if (x64) {
    switch (reg) {
      case kCDDwarfRegFrame: return 6;
      case kCDDwarfRegStack: return 7;
      case kCDDwarfRegIP: return 16;
    }
  } else {
    switch (reg) {
      case kCDDwarfRegFrame: return 5;
      case kCDDwarfRegStack: return 4;
      case kCDDwarfRegIP: return 8;
    }
  }
}


cd_error_t cd_dwarf_oreg(cd_obj_thread_t* thread,
                         int x64,
                         uint64_t reg,
                         uint64_t* res) {
  if (x64) {
    if (reg == 6)
      *res = thread->stack.frame;
    else if (reg == 7)
      *res = thread->stack.top;
    else if (reg == 16)
      *res = thread->regs.ip;
    else
      return cd_error_str(kCDErrNotFound, "reg not found");
  } else {
    if (reg == 5)
      *res = thread->stack.frame;
    else if (reg == 4)
      *res = thread->stack.top;
    else if (reg == 8)
      *res = thread->regs.ip;
    else
      return cd_error_str(kCDErrNotFound, "reg not found");
  }
  return cd_ok();
}


cd_error_t cd_dwarf_load(cd_dwarf_state_t* state,
                         cd_obj_thread_t* othread,
                         cd_obj_thread_t* nthread,
                         cd_dwarf_reg_t reg,
                         int x64,
                         char* stack,
                         uint64_t stack_size) {
  cd_error_t err;
  uint64_t* res;
  uint64_t off;
  cd_dwarf_loc_t* loc;

  off = cd_dwarf_reg_off(reg, x64);
  err = cd_dwarf_treg(nthread, reg, &res);
  if (!cd_is_ok(err))
    return err;

  loc = &state->regs[off];
  switch (loc->type) {
    case kCDDwarfLocReg:
      err = cd_dwarf_oreg(othread, x64, loc->reg, res);
      if (!cd_is_ok(err))
        return err;
      break;
    case kCDDwarfLocMem:
      *res = *(uint64_t*) &stack[loc->off];
      break;
    case kCDDwarfLocNone:
      {
        uint64_t* oval;

        err = cd_dwarf_treg(othread, reg, &oval);
        if (!cd_is_ok(err))
          return err;

        *res = *oval;
      }
      /* No operation */
      break;
    case kCDDwarfLocUndefined:
      *res = 0xdeadbeef;
      break;
    default:
      return cd_error_str(kCDErrDwarfNoCFA, "FDE's IP uses invalid loc type");
  }

  return cd_ok();
}


cd_error_t cd_dwarf_fde_run(cd_dwarf_fde_t* fde,
                            char* stack,
                            uint64_t stack_size,
                            uint64_t stack_off,
                            cd_obj_thread_t* othread,
                            cd_obj_thread_t* nthread) {
  cd_error_t err;
  cd_dwarf_state_t ist;
  cd_dwarf_state_t fst;

  memset(&ist, 0, sizeof(ist));

  /* Get defaults */
  err = cd_dwarf_run(fde->cie,
                     fde->cie->instrs,
                     fde->cie->instr_len,
                     othread->regs.ip,
                     NULL,
                     &ist);
  if (!cd_is_ok(err))
    return err;

  /* Copy default values */
  fst = ist;
  fst.loc = fde->init_loc + fde->cie->cfa->obj->aslr;

  /* Get FDE specific stuff */
  err = cd_dwarf_run(fde->cie,
                     fde->instrs,
                     fde->instr_len,
                     othread->regs.ip,
                     NULL,
                     &fst);
  if (!cd_is_ok(err))
    return err;

  if (fst.cfa.type == kCDDwarfLocNone)
    return cd_error_str(kCDErrDwarfNoCFA, "No CFA in CIE and FDE");

  /* Get new stack top */
  err = cd_dwarf_oreg(othread,
                      fde->cie->cfa->obj->is_x64,
                      fst.cfa.reg,
                      &nthread->stack.top);
  if (!cd_is_ok(err))
    return err;
  nthread->stack.top += fst.cfa.off;

  stack += nthread->stack.top - stack_off;
  stack_size -= nthread->stack.top - stack_off;

  /* Get IP */
  err = cd_dwarf_load(&fst,
                      othread,
                      nthread,
                      kCDDwarfRegIP,
                      fde->cie->cfa->obj->is_x64,
                      stack,
                      stack_size);
  if (!cd_is_ok(err))
    return err;

  /* Get frame ptr */
  err = cd_dwarf_load(&fst,
                      othread,
                      nthread,
                      kCDDwarfRegFrame,
                      fde->cie->cfa->obj->is_x64,
                      stack,
                      stack_size);
  if (!cd_is_ok(err))
    return err;

  return cd_ok();
}
