#ifndef SRC_COLLECTOR_H_
#define SRC_COLLECTOR_H_

#include "error.h"
#include "queue.h"
#include "v8helpers.h"

#include <stdint.h>

/* Forward-declarations */
struct cd_state_s;

typedef struct cd_js_frame_s cd_js_frame_t;

struct cd_js_frame_s {
  QUEUE member;

  char* start;
  char* stop;
  uint64_t ip;

  const char* name;
  int name_len;

  cd_script_t script;
};

/* Collect roots on the stack of the core file */

cd_error_t cd_collector_init(struct cd_state_s* state);
void cd_collector_destroy(struct cd_state_s* state);

cd_error_t cd_collect_roots(struct cd_state_s* state);

#endif  /* SRC_COLLECTOR_H_ */
