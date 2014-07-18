#ifndef SRC_STATE_H_
#define SRC_STATE_H_

#include "obj.h"
#include "common.h"
#include "strings.h"
#include "queue.h"

typedef struct cd_state_s cd_state_t;

struct cd_state_s {
  cd_obj_t* core;
  cd_obj_t* binary;
  int output;
  int ptr_size;

  QUEUE queue;
  QUEUE nodes;
  int node_count;

  cd_strings_t strings;

  intptr_t zap_bit;
};

#endif  /* SRC_STATE_H_ */
