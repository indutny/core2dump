#ifndef SRC_OBJ_COMMON_H_
#define SRC_OBJ_COMMON_H_

/* Forward declarations */
struct cd_obj_s;

typedef struct cd_obj_thread_s cd_obj_thread_t;
typedef struct cd_frame_s cd_frame_t;

typedef cd_error_t (*cd_iterate_stack_cb)(struct cd_obj_s* obj,
                                          cd_frame_t* frame,
                                          void* arg);


struct cd_obj_thread_s {
  struct {
    unsigned int count;
    /* XXX Support variable register count? */
    uint64_t values[32];

    uint64_t ip;
  } regs;

  struct {
    uint64_t top;
    uint64_t frame;
    uint64_t bottom;
  } stack;
};

struct cd_frame_s {
  char* start;
  char* stop;
  uint64_t ip;
};

#endif  /* SRC_OBJ_COMMON_H_ */
