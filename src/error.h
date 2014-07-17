#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

typedef struct cd_error_s cd_error_t;
typedef enum cd_error_code_e cd_error_code_t;

enum cd_error_code_e {
  kCDErrOk = 0x0,
  kCDErrInputNotFound = 0x1,
  kCDErrBinaryNotFound = 0x2,
  kCDErrOutputNotFound = 0x3,
  kCDErrBigEndianMagic = 0x4,
  kCDErrInvalidMagic = 0x5,
  kCDErrNotEnoughMagic = 0x6,
  kCDErrNoMem = 0x7,
  kCDErrFStat = 0x8,
  kCDErrMmap = 0x9,
  kCDErrNotCore = 0xa,
  kCDErrLoadCommandOOB = 0xb,
  kCDErrNotFound = 0xc,
  kCDErrSymtabOOB = 0xd,
  kCDErrThreadStateOOB = 0xe,
  kCDErrThreadStateInvalidSize = 0xf
};

struct cd_error_s {
  cd_error_code_t code;
  int num;
  const char* reason;
};

int cd_is_ok(cd_error_t err);
cd_error_t cd_ok();
cd_error_t cd_error(cd_error_code_t code);
cd_error_t cd_error_num(cd_error_code_t code, int num);
cd_error_t cd_error_str(cd_error_code_t code, const char* str);

#endif  /* SRC_ERROR_H_ */
