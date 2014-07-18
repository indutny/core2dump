#ifndef SRC_COLLECTOR_H_
#define SRC_COLLECTOR_H_

#include "error.h"
#include "queue.h"
#include "state.h"

typedef struct cd_collect_item_s cd_collect_item_t;

struct cd_collect_item_s {
  void* obj;
  QUEUE member;
};

/* Collect roots on the stack of the core file */

cd_error_t cd_collector_init(cd_state_t* state);
void cd_collector_destroy(cd_state_t* state);

cd_error_t cd_collect_roots(cd_state_t* state);

#endif  /* SRC_COLLECTOR_H_ */
