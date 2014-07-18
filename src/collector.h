#ifndef SRC_COLLECTOR_H_
#define SRC_COLLECTOR_H_

#include "error.h"
#include "state.h"

/* Collect roots on the stack of the core file */

cd_error_t cd_collect_roots(cd_state_t* state);

#endif  /* SRC_COLLECTOR_H_ */
