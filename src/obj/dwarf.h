#ifndef SRC_OBJ_DWARF_H_
#define SRC_OBJ_DWARF_H_

#include "error.h"
#include "queue.h"
#include "common.h"

#include <stdint.h>

/* Forward declarations */
struct cd_obj_s;

typedef struct cd_dwarf_cfa_s cd_dwarf_cfa_t;
typedef struct cd_dwarf_cie_s cd_dwarf_cie_t;
typedef struct cd_dwarf_fde_s cd_dwarf_fde_t;
typedef enum cd_dwarf_enc_e cd_dwarf_enc_t;

struct cd_dwarf_cfa_s {
  QUEUE cies;

  struct cd_obj_s* obj;
  char* start;
  uint64_t sect_addr;

  cd_splay_t fde_splay;
};

struct cd_dwarf_cie_s {
  QUEUE member;
  QUEUE fdes;

  cd_dwarf_cfa_t* cfa;
  uint64_t len;
  uint64_t id;
  uint8_t version;
  const char* augment;
  uint64_t code_align;
  int64_t data_align;
  uint64_t ret_reg;

  uint64_t aug_len;
  char* aug_data;
  uint8_t fde_enc;
};

struct cd_dwarf_fde_s {
  QUEUE member;

  cd_dwarf_cie_t* cie;
  uint64_t len;
  uint64_t cie_off;
  uint64_t init_loc;
  uint64_t range;

  uint64_t aug_len;
  const char* aug_data;

  uint64_t instr_len;
  const char* instrs;
};

enum cd_dwarf_enc_e {
  kCDDwarfEncEncodeMask = 0x0f,
  kCDDwarfEncAppMask = 0xf0,

  kCDDwarfEncAbsPtr = 0x00,
  kCDDwarfEncULeb128 = 0x01,
  kCDDwarfEncUData2 = 0x02,
  kCDDwarfEncUData4 = 0x03,
  kCDDwarfEncUData8 = 0x04,
  kCDDwarfEncSigned = 0x08,
  kCDDwarfEncSLeb128 = 0x09,
  kCDDwarfEncSData2 = 0x0a,
  kCDDwarfEncSData4 = 0x0b,
  kCDDwarfEncSData8 = 0x0c,

  kCDDwarfEncPCrel = 0x10,
  kCDDwarfEncTextrel = 0x20,
  kCDDwarfEncDatarel = 0x30,
  kCDDwarfEncFuncrel = 0x40,
  kCDDwarfEncAligned = 0x50,
  kCDDwarfEncIndirect = 0x80
};


cd_error_t cd_dwarf_parse_cfa(struct cd_obj_s* obj,
                              uint64_t sect_addr,
                              void* data,
                              uint64_t size,
                              cd_dwarf_cfa_t** res);
void cd_dwarf_free_cfa(cd_dwarf_cfa_t* cfa);


#endif  /* SRC_OBJ_DWARF_H_ */
