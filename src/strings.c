#include "strings.h"
#include "common.h"
#include "error.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cd_error_t cd_strings_init(cd_strings_t* strings) {
  QUEUE_INIT(&strings->queue);

  if (cd_hashmap_init(&strings->map, 128) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_t strings");

  return cd_ok();
}


void cd_strings_destroy(cd_strings_t* strings) {
  while (!QUEUE_EMPTY(&strings->queue)) {
    QUEUE* q;
    cd_strings_item_t* item;

    q = QUEUE_HEAD(&strings->queue);
    QUEUE_REMOVE(q);

    item = container_of(q, cd_strings_item_t, member);
    free(item);
  }

  cd_hashmap_destroy(&strings->map);
}


cd_error_t cd_strings_copy(cd_strings_t* strings,
                           const char** res,
                           int* index,
                           const char* str,
                           int len) {
  int r;
  cd_strings_item_t* item;

  /* Check if the string is already known */
  item = cd_hashmap_get(&strings->map, str, len);
  if (item != NULL) {
    *index = item->index;
    *res = item->str;

    /* Return existing string */
    return cd_ok();
  }

  /* Duplicate string and insert into the list and hashmap */
  item = malloc(sizeof(*item) + len);
  if (item == NULL)
    return cd_error_str(kCDErrNoMem, "strdup failure");
  memcpy(item->str, str, len);

  r = cd_hashmap_insert(&strings->map, *res, len, item);
  if (r != 0) {
    free(item);
    return cd_error_str(kCDErrNoMem, "cd_list_push strings.list failure");
  }

  item->str[len] = '\0';
  item->index = strings->count++;
  QUEUE_INSERT_TAIL(&strings->queue, &item->member);

  *res = item->str;
  *index = item->index;

  return cd_ok();
}


void cd_strings_print(cd_strings_t* strings, int fd) {
  QUEUE* q;

  QUEUE_FOREACH(q, &strings->queue) {
    cd_strings_item_t* item;

    item = container_of(q, cd_strings_item_t, member);
    if (q == QUEUE_PREV(&strings->queue))
      dprintf(fd, "\"%s\"", item->str);
    else
      dprintf(fd, "\"%s\", ", item->str);
  }
}
