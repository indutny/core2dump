#ifndef SRC_OBJ_H_
#define SRC_OBJ_H_

#include <stdint.h>

#include "error.h"

/* Forward declaration */
struct cd_obj_s;

typedef struct cd_obj_s cd_obj_t;
typedef cd_error_t (*cd_obj_iterate_cb)(void* arg, void* addr, uint64_t size);

cd_obj_t* cd_obj_new(int fd, cd_error_t* err);
void cd_obj_free(cd_obj_t* obj);
int cd_obj_is_x64(cd_obj_t* obj);

cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res);
cd_error_t cd_obj_get_sym(cd_obj_t* obj, const char* sym, uint64_t* addr);

cd_error_t cd_obj_iterate(cd_obj_t* obj, cd_obj_iterate_cb cb, void* arg);

#endif  /* SRC_OBJ_H_ */
