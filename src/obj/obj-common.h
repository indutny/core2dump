#ifndef SRC_OBJ_OBJ_COMMON_H_
#define SRC_OBJ_OBJ_COMMON_H_

#include "error.h"

#include <stdint.h>

/* Forward declarations */
struct cd_obj_s;

typedef struct cd_segment_s cd_segment_t;
typedef cd_error_t (*cd_obj_iterate_sym_cb)(struct cd_obj_s* obj,
                                            const char* name,
                                            int nlen,
                                            uint64_t value,
                                            void* arg);

#define CD_OBJ_COMMON_FIELDS                                                  \
    void* addr;                                                               \
    size_t size;                                                              \
    int is_x64;                                                               \
    cd_hashmap_t syms;                                                        \
    int has_syms;                                                             \
    cd_segment_t* segments;                                                   \
    int segment_count;                                                        \
    cd_splay_t seg_splay;                                                     \


struct cd_segment_s {
  uint64_t start;
  uint64_t end;

  char* ptr;
};

int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b);

/* Platform-specific */
cd_error_t cd_obj_iterate_syms(struct cd_obj_s* obj,
                               cd_obj_iterate_sym_cb cb,
                               void* arg);

#endif  /* SRC_OBJ_OBJ_COMMON_H_ */
