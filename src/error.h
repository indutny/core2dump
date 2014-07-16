#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

typedef struct cd_error_s cd_error_t;
typedef enum cd_error_code_e cd_error_code_t;

enum cd_error_code_e {
  kCDErrOk = 0x0,
  kCDErrInputNotFound = 0x1,
  kCDErrOutputNotFound = 0x2,
  kCDErrBigEndianMagic = 0x3,
  kCDErrInvalidMagic = 0x4,
  kCDErrNoMem = 0x5,
  kCDErrPread = 0x6,
  kCDErrPreadNotEnough = 0x7,
  kCDErrNotCore = 0x8,
  kCDErrCmdNotEnough = 0x9,  /* Not really a error */
  kCDErrCmdZeroSize = 0xa,
  kCDErrCmdSmallerThanExpected = 0xb,
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
