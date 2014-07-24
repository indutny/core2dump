#ifndef SRC_OBJ_H_
#define SRC_OBJ_H_

#include <stdint.h>

#include "error.h"

/* Forward declaration */
struct cd_obj_s;

typedef struct cd_obj_thread_s cd_obj_thread_t;

struct cd_obj_thread_s {
  struct {
    unsigned int count;
    /* XXX Support variable register count? */
    uint64_t values[32];
  } regs;

  struct {
    uint64_t top;
    uint64_t frame;
    uint64_t bottom;
  } stack;
};

typedef struct cd_obj_s cd_obj_t;

cd_obj_t* cd_obj_new(int fd, cd_error_t* err);
void cd_obj_free(cd_obj_t* obj);
int cd_obj_is_x64(cd_obj_t* obj);

cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res);
cd_error_t cd_obj_get_sym(cd_obj_t* obj, const char* sym, uint64_t* addr);
cd_error_t cd_obj_get_thread(cd_obj_t* obj,
                             unsigned int index,
                             cd_obj_thread_t* thread);

#endif  /* SRC_OBJ_H_ */
