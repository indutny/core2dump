#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

typedef struct cd_error_s cd_error_t;
typedef enum cd_error_code_e cd_error_code_t;

#define CD_ERROR_ENUM(V)                                                      \
    V(ErrOk, 0x0)                                                             \
    V(ErrInputNotFound, 0x1)                                                  \
    V(ErrBinaryNotFound, 0x2)                                                 \
    V(ErrOutputNotFound, 0x3)                                                 \
    V(ErrBigEndianMagic, 0x4)                                                 \
    V(ErrInvalidMagic, 0x5)                                                   \
    V(ErrNotEnoughMagic, 0x6)                                                 \
    V(ErrNoMem, 0x7)                                                          \
    V(ErrFStat, 0x8)                                                          \
    V(ErrMmap, 0x9)                                                           \
    V(ErrNotCore, 0xa)                                                        \
    V(ErrLoadCommandOOB, 0xb)                                                 \
    V(ErrNotFound, 0xc)                                                       \
    V(ErrSymtabOOB, 0xd)                                                      \
    V(ErrThreadStateOOB, 0xe)                                                 \
    V(ErrThreadStateInvalidSize, 0xf)                                         \
    V(ErrNotObject, 0x10)                                                     \
    V(ErrUnknownObjectType, 0x11)                                             \
    V(ErrListShift, 0x12)                                                     \
    V(ErrNotString, 0x13)                                                     \
    V(ErrAlreadyVisited, 0x14)                                                \
    V(ErrNotSoFast, 0x15)                                                     \
    V(ErrNotSoSlow, 0x16)                                                     \
    V(ErrUnsupportedElements, 0x17)                                           \

#define CD_ERROR_DECL(X, Y) kCD##X = Y,

enum cd_error_code_e {
  CD_ERROR_ENUM(CD_ERROR_DECL)
  kCDErrLast
};

#undef CD_ERROR_DECL

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
const char* cd_error_to_str(cd_error_t err);

#endif  /* SRC_ERROR_H_ */
