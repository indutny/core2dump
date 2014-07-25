#ifndef SRC_COLLECTOR_H_
#define SRC_COLLECTOR_H_

#include "error.h"
#include "queue.h"
#include "state.h"

typedef void (*cd_iterate_stack_cb)(char* start,
                                    char* stop,
                                    uint64_t ip,
                                    void* arg);

/* Collect roots on the stack of the core file */

cd_error_t cd_collector_init(cd_state_t* state);
void cd_collector_destroy(cd_state_t* state);

cd_error_t cd_collect_roots(cd_state_t* state);
cd_error_t cd_iterate_stack(cd_state_t* state,
                            cd_iterate_stack_cb cb,
                            void* arg);

#endif  /* SRC_COLLECTOR_H_ */
