#ifndef SRC_OBJ_DWARF_H_
#define SRC_OBJ_DWARF_H_

#include "error.h"
#include "queue.h"

#include <stdint.h>

/* Forward declarations */
struct cd_obj_s;

typedef struct cd_dwarf_cfa_s cd_dwarf_cfa_t;
typedef struct cd_dwarf_cie_s cd_dwarf_cie_t;
typedef struct cd_dwarf_fde_s cd_dwarf_fde_t;

struct cd_dwarf_cfa_s {
  QUEUE cies;

  struct cd_obj_s* obj;
};

struct cd_dwarf_cie_s {
  QUEUE member;
  QUEUE fdes;

  cd_dwarf_cfa_t* cfa;
  uint64_t len;
  uint64_t id;
  uint8_t version;
  const char* augment;
  uint16_t code_align;
  int16_t data_align;
  uint16_t ret_reg;

  uint16_t aug_len;
  const char* aug_data;
  uint8_t fde_enc;
};

struct cd_dwarf_fde_s {
  QUEUE member;

  cd_dwarf_cie_t* cie;
  uint64_t len;
  uint64_t cie_off;
  uint64_t init_loc;
  uint64_t range;

  uint16_t aug_len;
  const char* aug_data;

  uint64_t instr_len;
  const char* instrs;
};

static const int kCDDwarfEncodeMask = 0x0f;
static const int kCDDwarfAppMask = 0xf0;

#define kCDDwarfAbsPtr 0x00
#define kCDDwarfPCRel 0x10

cd_error_t cd_dwarf_parse_cfa(struct cd_obj_s* obj,
                              void* data,
                              uint64_t size,
                              cd_dwarf_cfa_t** res);
void cd_dwarf_free_cfa(cd_dwarf_cfa_t* cfa);


#endif  /* SRC_OBJ_DWARF_H_ */
