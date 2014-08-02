#ifndef SRC_OBJ_OBJ_COMMON_H_
#define SRC_OBJ_OBJ_COMMON_H_

#include "error.h"

#include <stdint.h>

/* Forward declarations */
struct cd_obj_s;

typedef struct cd_segment_s cd_segment_t;
typedef struct cd_sym_s cd_sym_t;
typedef cd_error_t (*cd_obj_iterate_sym_cb)(struct cd_obj_s* obj,
                                            cd_sym_t* sym,
                                            void* arg);
typedef cd_error_t (*cd_obj_iterate_seg_cb)(struct cd_obj_s* obj,
                                            cd_segment_t* seg,
                                            void* arg);

#define CD_OBJ_COMMON_FIELDS                                                  \
    void* addr;                                                               \
    size_t size;                                                              \
    int is_x64;                                                               \
    cd_hashmap_t syms;                                                        \
    cd_splay_t sym_splay;                                                     \
    int has_syms;                                                             \
    cd_segment_t* segments;                                                   \
    int segment_count;                                                        \
    cd_splay_t seg_splay;                                                     \


struct cd_segment_s {
  uint64_t start;
  uint64_t end;

  char* ptr;
};

struct cd_sym_s {
  const char* name;
  int nlen;
  uint64_t value;
};

cd_error_t cd_obj_common_init(struct cd_obj_s* obj);
void cd_obj_common_free(struct cd_obj_s* obj);

/* Platform-specific */
cd_error_t cd_obj_iterate_syms(struct cd_obj_s* obj,
                               cd_obj_iterate_sym_cb cb,
                               void* arg);
cd_error_t cd_obj_iterate_segs(struct cd_obj_s* obj,
                               cd_obj_iterate_seg_cb cb,
                               void* arg);

/* Internal, mostly */
cd_error_t cd_obj_init_segments(cd_common_obj_t* obj);

#endif  /* SRC_OBJ_OBJ_COMMON_H_ */
