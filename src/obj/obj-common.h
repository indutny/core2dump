#ifndef SRC_OBJ_OBJ_COMMON_H_
#define SRC_OBJ_OBJ_COMMON_H_

typedef struct cd_segment_s cd_segment_t;

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

static int cd_segment_sort(const cd_segment_t* a, const cd_segment_t* b) {
  return a->start > b->start ? 1 : a->start == b->start ? 0 : -1;
}

#endif  /* SRC_OBJ_OBJ_COMMON_H_ */
