#ifndef SRC_STATE_H_
#define SRC_STATE_H_

#include "obj.h"
#include "common.h"

typedef struct cd_state_s cd_state_t;

struct cd_state_s {
  cd_obj_t* core;
  cd_obj_t* binary;
  int output;
  int ptr_size;

  cd_list_t queue;
  cd_list_t nodes;

  struct {
    cd_list_t list;
    cd_hashmap_t map;
  } strings;

  intptr_t zap_bit;
};

#endif  /* SRC_STATE_H_ */
