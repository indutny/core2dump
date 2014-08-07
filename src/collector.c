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


static cd_error_t cd_collect_frame(cd_obj_t* obj,
                                   cd_frame_t* frame,
                                   void* arg);
static cd_error_t cd_collect_v8_frame(cd_state_t* state,
                                      cd_js_frame_t* frame);


cd_error_t cd_collector_init(cd_state_t* state) {
  QUEUE_INIT(&state->queue);
  QUEUE_INIT(&state->frames);
  state->frame_count = 0;
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
    cd_js_frame_t* frame;

    q = QUEUE_HEAD(&state->frames);
    QUEUE_REMOVE(q);

    frame = container_of(q, cd_js_frame_t, member);
    free(frame);
  }
}


cd_error_t cd_collect_frame(cd_obj_t* obj, cd_frame_t* sframe, void* arg) {
  cd_state_t* state;
  cd_error_t err;
  cd_js_frame_t* frame;

  state = (cd_state_t*) arg;

  frame = malloc(sizeof(*frame));
  if (frame == NULL)
    return cd_error_str(kCDErrNoMem, "cd_js_frame_t");

  /* Copy the data */
  frame->start = sframe->start;
  frame->stop = sframe->stop;
  frame->ip = sframe->ip;

  /* Lookup C/C++ symbol if present */
  if (sframe->sym != NULL) {
    frame->name = sframe->sym;
    frame->name_len = sframe->sym_len;
    err = cd_ok();
    goto done;
  }

  /* Inspect v8 frame */
  frame->name = NULL;
  frame->name_len = 0;

  err = cd_collect_v8_frame(state, frame);
  if (!cd_is_ok(err)) {
    free(frame);
    return cd_ok();
  }

done:
  QUEUE_INSERT_TAIL(&state->frames, &frame->member);
  state->frame_count++;
  return cd_ok();
}


#define CFRAME(frame, str)                                                    \
    do {                                                                      \
      (frame)->name = (str);                                                  \
      (frame)->name_len = sizeof(str) - 1;                                    \
      return cd_ok();                                                         \
    } while (0)                                                               \



cd_error_t cd_collect_v8_frame(cd_state_t* state, cd_js_frame_t* frame) {
  cd_error_t err;
  void* ctx;
  void* marker;
  void* fn;
  void* args;
  int type;
  unsigned int i;
  cd_obj_thread_t thread;
  cd_node_t* fn_node;

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

  err = cd_queue_ptr(state,
                     &state->nodes.root,
                     fn,
                     NULL,
                     kCDEdgeElement,
                     state->frame_count,
                     0,
                     &fn_node);
  if (!cd_is_ok(err))
    fn_node = &state->nodes.root;

  /* Visit all pointers in a frame, except fn */
  for (i = 0;
      frame->start != frame->stop;
      frame->start -= state->ptr_size, i++) {
    void* ptr;

    ptr = *(void**) frame->start;
    if (ptr == fn)
      continue;

    cd_queue_ptr(state,
                 fn_node,
                 ptr,
                 NULL,
                 kCDEdgeElement,
                 i,
                 0,
                 NULL);
  }

  /* First frame - collect registers too */
  if (state->frame_count == 0) {
    unsigned int j;
    err = cd_obj_get_thread(state->core, 0, &thread);
    if (!cd_is_ok(err))
      return err;

    /* Visit registers */
    for (j = 0; j < thread.regs.count; j++) {
      void* ptr;

      ptr = (void*) (intptr_t) thread.regs.values[j];
      cd_queue_ptr(state,
                   fn_node,
                   ptr,
                   NULL,
                   kCDEdgeElement,
                   i + j,
                   0,
                   NULL);
    }
  }

  if (type == CD_V8_TYPE(Code, CODE))
    CFRAME(frame, "<internal code>");

  err = cd_v8_fn_info(state, fn, &frame->name, NULL, NULL, &frame->script);
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

  err = cd_v8_init(state->core);
  if (!cd_is_ok(err))
    return err;

  return cd_obj_iterate_stack(state->core, 0, cd_collect_frame, state);
}
