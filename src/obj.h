#ifndef SRC_OBJ_H_
#define SRC_OBJ_H_

#include <stdint.h>

#include "error.h"
#include "obj-common.h"
#include "obj-internal.h"

/* Forward declaration */
struct cd_obj_method_s;
struct cd_dwarf_fde_s;

typedef struct cd_obj_s cd_obj_t;

struct cd_obj_s {
  CD_OBJ_INTERNAL_FIELDS
};

cd_obj_t* cd_obj_new(struct cd_obj_method_s* method,
                     const char* path,
                     cd_error_t* err);
void cd_obj_free(cd_obj_t* obj);
int cd_obj_is_x64(cd_obj_t* obj);
int cd_obj_is_core(cd_obj_t* obj);
cd_error_t cd_obj_add_dso(cd_obj_t* obj, cd_obj_t* dso);

cd_error_t cd_obj_get(cd_obj_t* obj, uint64_t addr, uint64_t size, void** res);
cd_error_t cd_obj_get_sym(cd_obj_t* obj, const char* sym, uint64_t* addr);
cd_error_t cd_obj_lookup_ip(cd_obj_t* obj,
                            uint64_t addr,
                            const char** sym,
                            int* sym_len,
                            struct cd_dwarf_fde_s* fde);
cd_error_t cd_obj_get_thread(cd_obj_t* obj,
                             unsigned int index,
                             cd_obj_thread_t* thread);
cd_error_t cd_obj_iterate_stack(cd_obj_t* obj,
                                int thread_id,
                                cd_iterate_stack_cb cb,
                                void* arg);

#endif  /* SRC_OBJ_H_ */
