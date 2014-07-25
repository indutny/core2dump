#include "collector.h"
#include "error.h"
#include "obj.h"
#include "queue.h"
#include "state.h"
#include "v8constants.h"
#include "v8helpers.h"
#include "visitor.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>


cd_error_t cd_collector_init(cd_state_t* state) {
  QUEUE_INIT(&state->queue);
  return cd_ok();
}


void cd_collector_destroy(cd_state_t* state) {
  while (!QUEUE_EMPTY(&state->queue)) {
    QUEUE* q;
    cd_node_t* node;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);

    node = container_of(q, cd_node_t, member);
    free(node);
  }
}


cd_error_t cd_collect_roots(cd_state_t* state) {
  cd_error_t err;
  uint64_t off;
  void* stack;
  uint64_t stack_size;
  unsigned int i;
  cd_obj_thread_t thread;

  err = cd_obj_get_thread(state->core, 0, &thread);
  if (!cd_is_ok(err))
    return err;

  /* Visit stack */
  stack_size = thread.stack.bottom - thread.stack.top;
  err = cd_obj_get(state->core,
                   thread.stack.top,
                   stack_size,
                   &stack);
  if (!cd_is_ok(err))
    return err;

  for (off = 0; off < stack_size; off += state->ptr_size) {
    void* ptr;

    ptr = *(void**)((char*) stack + stack_size - off);
    cd_queue_ptr(state,
                 &state->nodes.root,
                 ptr,
                 NULL,
                 kCDEdgeElement,
                 thread.regs.count + (off / state->ptr_size),
                 0,
                 NULL);
  }

  /* Visit registers */
  for (i = 0; i < thread.regs.count; i++) {
    void* ptr;

    ptr = (void*) (intptr_t) thread.regs.values[i];
    cd_queue_ptr(state,
                 &state->nodes.root,
                 ptr,
                 NULL,
                 kCDEdgeElement,
                 i,
                 0,
                 NULL);
  }

  return cd_ok();
}


cd_error_t cd_iterate_stack(cd_state_t* state,
                            cd_iterate_stack_cb cb,
                            void* arg) {
  cd_error_t err;
  cd_obj_thread_t thread;
  char* stack;
  uint64_t stack_size;
  uint64_t frame_start;
  uint64_t frame_end;
  uint64_t ip;

  err = cd_v8_init(state->binary, state->core);
  if (!cd_is_ok(err))
    return err;

  err = cd_obj_get_thread(state->core, 0, &thread);
  if (!cd_is_ok(err))
    return err;

  stack_size = thread.stack.bottom - thread.stack.top;
  err = cd_obj_get(state->core,
                   thread.stack.top,
                   stack_size,
                   (void**) &stack);
  if (!cd_is_ok(err))
    return err;

  if (thread.stack.frame <= thread.stack.top)
    return cd_error(kCDErrStackOOB);

  frame_start = thread.stack.frame - thread.stack.top;
  frame_end = 0;
  ip = thread.regs.ip;
  while (frame_start < stack_size) {
    cb(stack + frame_start, stack + frame_end, ip, arg);

    /* Next frame */
    ip = *(uint64_t*) (stack + frame_start + state->ptr_size);
    frame_end = frame_start;
    frame_start = *(uint64_t*) (stack + frame_start);
    if (frame_start <= thread.stack.top)
      break;

    frame_start -= thread.stack.top;
    if (frame_start >= stack_size)
      break;
  }

  return cd_error(kCDErrNotFound);
}
