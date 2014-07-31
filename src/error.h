#ifndef SRC_ERROR_H_
#define SRC_ERROR_H_

typedef struct cd_error_s cd_error_t;
typedef enum cd_error_code_e cd_error_code_t;

#define CD_ERROR_ENUM(V)                                                      \
    V(Ok, 0x0)                                                                \
    V(InputNotFound, 0x1)                                                     \
    V(BinaryNotFound, 0x2)                                                    \
    V(OutputNotFound, 0x3)                                                    \
    V(BigEndianMagic, 0x4)                                                    \
    V(InvalidMagic, 0x5)                                                      \
    V(NotEnoughMagic, 0x6)                                                    \
    V(NoMem, 0x7)                                                             \
    V(FStat, 0x8)                                                             \
    V(Mmap, 0x9)                                                              \
    V(NotCore, 0xa)                                                           \
    V(LoadCommandOOB, 0xb)                                                    \
    V(NotFound, 0xc)                                                          \
    V(SymtabOOB, 0xd)                                                         \
    V(ThreadStateOOB, 0xe)                                                    \
    V(ThreadStateInvalidSize, 0xf)                                            \
    V(NotObject, 0x10)                                                        \
    V(UnknownObjectType, 0x11)                                                \
    V(ListShift, 0x12)                                                        \
    V(NotString, 0x13)                                                        \
    V(AlreadyVisited, 0x14)                                                   \
    V(NotSoFast, 0x15)                                                        \
    V(NotSoSlow, 0x16)                                                        \
    V(UnsupportedElements, 0x17)                                              \
    V(StackOOB, 0x18)                                                         \
    V(NotSMI, 0x19)                                                           \

#define CD_ERROR_DECL(X, Y) kCDErr##X = Y,

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
