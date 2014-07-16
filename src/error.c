#include "error.h"

int cd_is_ok(cd_error_t err) {
  return err.code == kCDErrOk;
}


cd_error_t cd_ok() {
  cd_error_t err;

  err.code = kCDErrOk;
  return err;
}


cd_error_t cd_error(cd_error_code_t code) {
  cd_error_t err;

  err.code = code;
  return err;
}


cd_error_t cd_error_num(cd_error_code_t code, int num) {
  cd_error_t err;

  err.code = code;
  err.num = num;
  return err;
}


cd_error_t cd_error_str(cd_error_code_t code, const char* str) {
  cd_error_t err;

  err.code = code;
  err.reason = str;
  return err;
}
