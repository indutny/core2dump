#include "collector.h"
#include "error.h"
#include "obj.h"
#include "state.h"
#include "v8constants.h"
#include "v8helpers.h"

#include <sys/types.h>


static cd_error_t cd_collect_root(cd_state_t* state, void* ptr);


cd_error_t cd_collect_roots(cd_state_t* state) {
  cd_error_t err;
  uint64_t off;
  void* stack;
  size_t stack_size;
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

  for (off = 0; off < stack_size; off += state->ptr_size)
    cd_collect_root(state, *(void**)((char*) stack + off));

  /* Visit registers */
  for (i = 0; i < thread.regs.count; i++)
    cd_collect_root(state, (void*) (intptr_t) thread.regs.values[i]);

  return cd_ok();
}


cd_error_t cd_collect_root(cd_state_t* state, void* ptr) {
  void* obj;
  void** pmap;
  void* map;
  uint8_t* attrs;

  obj = ptr;

  /* Find v8 heapobject */
  if (!V8_IS_HEAPOBJECT(obj))
    return cd_error(kCDErrNotObject);

  V8_CORE_PTR(obj, cd_v8_class_HeapObject__map__Map, pmap);
  map = *pmap;

  /* That has a heapobject map */
  if (!V8_IS_HEAPOBJECT(map))
    return cd_error(kCDErrNotObject);

  /* Just to verify that the object has live map */
  V8_CORE_PTR(map, cd_v8_class_Map__instance_attributes__int, attrs);

  if (cd_list_push(&state->queue, &obj) != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push queue");

  return cd_ok();
}
