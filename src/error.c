#include "error.h"

#include <stdlib.h>
#include <stdio.h>

int cd_is_ok(cd_error_t err) {
  return err.code == kCDErrOk;
}


cd_error_t cd_ok() {
  cd_error_t err;

  err.code = kCDErrOk;
  err.num = 0;
  err.reason = NULL;
  return err;
}


cd_error_t cd_error(cd_error_code_t code) {
  cd_error_t err;

  err.code = code;
  err.num = 0;
  err.reason = NULL;
  return err;
}


cd_error_t cd_error_num(cd_error_code_t code, int num) {
  cd_error_t err;

  err.code = code;
  err.num = num;
  err.reason = NULL;
  return err;
}


cd_error_t cd_error_str(cd_error_code_t code, const char* str) {
  cd_error_t err;

  err.code = code;
  err.reason = str;
  err.num = 0;
  return err;
}


#define CD_ERROR_TO_STR(X, Y) case kCD##X: name = #X; break;


const char* cd_error_to_str(cd_error_t err) {
  static char st[1024];
  const char* name;

  switch (err.code) {
    CD_ERROR_ENUM(CD_ERROR_TO_STR)
    default: name = "unknown"; break;
  }

  snprintf(st,
           sizeof(st),
           "Error: \"%s\" (%d) reason: \"%s\" num: (%d)",
           name,
           err.code,
           err.reason,
           err.num);

  return st;
}
