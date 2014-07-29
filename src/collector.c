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
#include <string.h>
#include <sys/types.h>


static void cd_collect_frame(cd_state_t* state,
                             cd_stack_frame_t* frame,
                             void* arg);
static cd_error_t cd_collect_v8_frame(cd_state_t* state,
                                      cd_stack_frame_t* frame);


cd_error_t cd_collector_init(cd_state_t* state) {
  QUEUE_INIT(&state->queue);
  QUEUE_INIT(&state->frames);
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

  while (!QUEUE_EMPTY(&state->frames)) {
    QUEUE* q;
    cd_stack_frame_t* frame;

    q = QUEUE_HEAD(&state->queue);
    QUEUE_REMOVE(q);

    frame = container_of(q, cd_stack_frame_t, member);
    free(frame);
  }
}


void cd_collect_frame(cd_state_t* state, cd_stack_frame_t* sframe, void* arg) {
  cd_error_t err;
  cd_stack_frame_t* frame;

  frame = malloc(sizeof(*frame));
  if (frame == NULL)
    return;

  /* Copy the data */
  *frame = *sframe;

  /* Lookup C/C++ symbol if present */
  err = cd_obj_lookup_ip(state->binary,
                         frame->ip,
                         &frame->name,
                         &frame->name_len);
  if (cd_is_ok(err)) {
    fprintf(stdout, "%.*s\n", frame->name_len, frame->name);
    goto done;
  }

  /* Inspect v8 frame */
  frame->name = NULL;
  frame->name_len = 0;

  err = cd_collect_v8_frame(state, frame);
  if (!cd_is_ok(err)) {
    free(frame);
    return;
  }
  fprintf(stdout, "%.*s\n", frame->name_len, frame->name);

done:
  QUEUE_INSERT_TAIL(&state->frames, &frame->member);
  return;
}


#define CFRAME(frame, str)                                                    \
    do {                                                                      \
      (frame)->name = (str);                                                  \
      (frame)->name_len = sizeof(str) - 1;                                    \
      return cd_ok();                                                         \
    } while (0)                                                               \



cd_error_t cd_collect_v8_frame(cd_state_t* state, cd_stack_frame_t* frame) {
  cd_error_t err;
  void* ctx;
  void* marker;
  void* fn;
  void* args;
  void* list[3];
  int type;
  unsigned int i;

  ctx = *(void**) (frame->start + cd_v8_off_fp_context);
  if (V8_IS_SMI(ctx) &&
      V8_SMI(ctx) == cd_v8_frametype_ArgumentsAdaptorFrame) {
    CFRAME(frame, "<adaptor>");
  }

  marker = *(void**) (frame->start + cd_v8_off_fp_marker);
  if (V8_IS_SMI(marker)) {
    int32_t m;
    m = V8_SMI(marker);
    if (m == cd_v8_frametype_EntryFrame) {
      CFRAME(frame, "<entry>");
    } else if (m == cd_v8_frametype_EntryConstructFrame) {
      CFRAME(frame, "<entry_construct>");
    } else if (m == cd_v8_frametype_ExitFrame) {
      CFRAME(frame, "<exit>");
    } else if (m == cd_v8_frametype_InternalFrame) {
      CFRAME(frame, "<internal>");
    } else if (m == cd_v8_frametype_ConstructFrame) {
      CFRAME(frame, "<constructor>");
    } else if (m != cd_v8_frametype_JavaScriptFrame &&
               m != cd_v8_frametype_OptimizedFrame) {
      return cd_error(kCDErrNotFound);
    }
  }

  fn = *(void**) (frame->start + cd_v8_off_fp_function);
  args = *(void**) (frame->start + cd_v8_off_fp_args);
  if (!V8_IS_HEAPOBJECT(fn) || !V8_IS_HEAPOBJECT(args))
    return cd_error(kCDErrNotFound);

  err = cd_v8_get_obj_type(state, fn, NULL, &type);
  if (!cd_is_ok(err))
    return err;

  list[0] = fn;
  list[1] = args;
  list[2] = ctx;
  for (i = 0; i < ARRAY_SIZE(list); i++) {
    cd_queue_ptr(state,
                 &state->nodes.root,
                 list[i],
                 NULL,
                 kCDEdgeElement,
                 i,
                 0,
                 NULL);
  }

  if (type == CD_V8_TYPE(Code, CODE))
    CFRAME(frame, "<internal code>");

  err = cd_v8_fn_name(state, fn, &frame->name, NULL);
  if (!cd_is_ok(err))
    return err;

  frame->name_len = strlen(frame->name);
  if (frame->name_len == 0)
    CFRAME(frame, "(anon)");

  return cd_ok();
}


#undef CFRAME


cd_error_t cd_collect_roots(cd_state_t* state) {
  cd_error_t err;
  unsigned int i;
  cd_obj_thread_t thread;

  err = cd_obj_get_thread(state->core, 0, &thread);
  if (!cd_is_ok(err))
    return err;

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

  return cd_iterate_stack(state, cd_collect_frame, NULL);
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
    cd_stack_frame_t frame;

    frame.start = stack + frame_start;
    frame.stop = stack + frame_end;
    frame.ip = ip;
    cb(state, &frame, arg);

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

  return cd_error(kCDErrOk);
}
