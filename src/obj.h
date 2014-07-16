#ifndef SRC_OBJ_H_
#define SRC_OBJ_H_

#include <sys/types.h>

#include "error.h"

/* Forward declaration */
struct cd_obj_s;

typedef struct cd_obj_s cd_obj_t;

cd_obj_t* cd_obj_new(int fd, cd_error_t* err);
void cd_obj_free(cd_obj_t* obj);

void* cd_obj_get(cd_obj_t* obj, intptr_t addr, size_t size);

/* Utils */
cd_error_t cd_pread(int fd, void* buf, size_t nbyte, off_t offset, int* read);

#endif  /* SRC_OBJ_H_ */
