#ifndef SRC_VISITOR_H_
#define SRC_VISITOR_H_

#include "common.h"
#include "error.h"
#include "state.h"

typedef struct cd_node_s cd_node_t;
typedef struct cd_edge_s cd_edge_t;

struct cd_node_s {
  void* obj;
  int type;

  cd_list_t edges;
};

struct cd_edge_s {
  void* from;
  void* to;
  int type;
  int index;
};

cd_error_t cd_visitor_init(cd_state_t* state);
void cd_visitor_destroy(cd_state_t* state);

cd_error_t cd_visit_roots(cd_state_t* state);

#endif  /* SRC_VISITOR_H_ */
