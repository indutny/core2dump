#ifndef SRC_OBJ_DWARF_H_
#define SRC_OBJ_DWARF_H_

#include "error.h"
#include "queue.h"
#include "common.h"

#include <stdint.h>

/* Forward declarations */
struct cd_obj_s;
struct cd_sym_s;

typedef struct cd_dwarf_cfa_s cd_dwarf_cfa_t;
typedef struct cd_dwarf_cie_s cd_dwarf_cie_t;
typedef struct cd_dwarf_fde_s cd_dwarf_fde_t;
typedef enum cd_dwarf_enc_e cd_dwarf_enc_t;
typedef enum cd_dwarf_cfa_instr_e cd_dwarf_cfa_instr_t;
typedef struct cd_dwarf_state_s cd_dwarf_state_t;
typedef enum cd_dwarf_loc_type_e cd_dwarf_loc_type_t;
typedef struct cd_dwarf_loc_s cd_dwarf_loc_t;

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

  uint64_t instr_len;
  char* instrs;
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
  char* instrs;
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

enum cd_dwarf_cfa_instr_e {
  kCDDwarfCFAHighMask = 0xc0,
  kCDDwarfCFALowMask = 0x3f,

  kCDDwarfCFAAdvanceLoc = 0x40,
  kCDDwarfCFAOffset = 0x80,
  kCDDwarfCFARestore = 0xc0,

  kCDDwarfCFANop = 0x00,
  kCDDwarfCFASetLoc = 0x01,
  kCDDwarfCFAAdvanceLoc1 = 0x02,
  kCDDwarfCFAAdvanceLoc2 = 0x03,
  kCDDwarfCFAAdvanceLoc4 = 0x04,
  kCDDwarfCFAOffsetExtended = 0x05,
  kCDDwarfCFARestoreExtended = 0x06,
  kCDDwarfCFAUndefined = 0x07,
  kCDDwarfCFASameValue = 0x08,
  kCDDwarfCFARegister = 0x09,
  kCDDwarfCFARememberState = 0x0a,
  kCDDwarfCFARestoreState = 0x0b,
  kCDDwarfCFADefCFA = 0x0c,
  kCDDwarfCFADefCFARegister = 0x0d,
  kCDDwarfCFADefCFAOffset = 0x0e,
  kCDDwarfCFADefCFAExpression = 0x0f,
  kCDDwarfCFAExpression = 0x10,
  kCDDwarfCFAOffsetExtendedSF = 0x11,
  kCDDwarfCFADefCFASF = 0x12,
  kCDDwarfCFADefCFAOffsetSF = 0x13,
  kCDDwarfCFAValOffset = 0x14,
  kCDDwarfCFAValOffsetSF = 0x15,
  kCDDwarfCFAValExpression = 0x16,
  kCDDwarfCFALoUser = 0x17,
  kCDDwarfCFAHiUser = 0x3f
};

enum cd_dwarf_loc_type_e {
  kCDDwarfLocNone = 0x0,
  kCDDwarfLocReg = 0x1,
  kCDDwarfLocMem = 0x2,
  kCDDwarfLocValue = 0x3,
  kCDDwarfLocUndefined = 0x4
};

struct cd_dwarf_loc_s {
  cd_dwarf_loc_type_t type;
  uint64_t reg;
  int64_t off;
};

struct cd_dwarf_state_s {
  uint64_t loc;

  cd_dwarf_loc_t cfa;
  cd_dwarf_loc_t regs[32];
};


cd_error_t cd_dwarf_parse_cfa(struct cd_obj_s* obj,
                              uint64_t sect_addr,
                              void* data,
                              uint64_t size,
                              cd_dwarf_cfa_t** res);
void cd_dwarf_free_cfa(cd_dwarf_cfa_t* cfa);

cd_error_t cd_dwarf_get_fde(cd_dwarf_cfa_t* cfa,
                            uint64_t addr,
                            cd_dwarf_fde_t** res);
cd_error_t cd_dwarf_fde_run(cd_dwarf_fde_t* fde,
                            uint64_t rip,
                            struct cd_sym_s* sym,
                            char* stack,
                            uint64_t stack_size,
                            uint64_t* frame,
                            uint64_t* ip);


#endif  /* SRC_OBJ_DWARF_H_ */
