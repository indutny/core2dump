#ifndef SRC_VISITOR_H_
#define SRC_VISITOR_H_

#include "common.h"
#include "error.h"
#include "state.h"

typedef struct cd_node_s cd_node_t;
typedef struct cd_edge_s cd_edge_t;
typedef enum cd_node_type_e cd_node_type_t;

enum cd_node_type_e {
  kCDNodeHidden,
  kCDNodeArray,
  kCDNodeString,
  kCDNodeObject,
  kCDNodeCode,
  kCDNodeClosure,
  kCDNodeRegExp,
  kCDNodeNumber
};

struct cd_node_s {
  void* obj;
  cd_node_type_t type;
  int id;
  int name;
  int size;

  QUEUE member;
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
