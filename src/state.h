#ifndef SRC_STATE_H_
#define SRC_STATE_H_

#include "obj.h"
#include "common.h"
#include "strings.h"
#include "queue.h"
#include "visitor.h"

typedef struct cd_state_s cd_state_t;

struct cd_state_s {
  cd_obj_t* core;
  cd_obj_t* binary;
  int output;
  int ptr_size;

  QUEUE queue;
  struct {
    int id;
    int count;
    cd_node_t root;
    QUEUE list;
    cd_hashmap_t map;
  } nodes;
  int edge_count;

  cd_strings_t strings;
};

#endif  /* SRC_STATE_H_ */
