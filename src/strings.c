#include "strings.h"
#include "common.h"
#include "error.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

cd_error_t cd_strings_init(cd_strings_t* strings) {
  cd_error_t err;

  if (cd_list_init(&strings->list, 128, sizeof(void*)) != 0) {
    err = cd_error_str(kCDErrNoMem, "cd_list_t strings");
    goto failed_strings_list_init;
  }

  if (cd_hashmap_init(&strings->map, 128) != 0) {
    err = cd_error_str(kCDErrNoMem, "cd_hashmap_t strings");
    goto failed_strings_map_init;
  }

  return cd_ok();

failed_strings_map_init:
  cd_list_free(&strings->list);

failed_strings_list_init:
  return err;
}


void cd_strings_destroy(cd_strings_t* strings) {
  while (cd_list_len(&strings->list) != 0) {
    void* ptr;

    cd_list_pop(&strings->list, &ptr);
    free(ptr);
  }
  cd_list_free(&strings->list);
  cd_hashmap_destroy(&strings->map);
}


cd_error_t cd_strings_copy(cd_strings_t* strings,
                           const char** res,
                           const char* str,
                           int len) {
  void* idx;
  int r;

  /* Check if the string is already known */
  idx = cd_hashmap_get(&strings->map, str, len);
  if (idx != NULL) {
    /* Return existing string */
    cd_list_get(&strings->list, (intptr_t) idx - 1, res);
    return cd_ok();
  }

  /* Duplicate string and insert into the list and hashmap */
  *res = strndup(str, len);
  if (*res == NULL)
    return cd_error_str(kCDErrNoMem, "strdup failure");

  r = cd_list_push(&strings->list, res);
  if (r != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push strings.list failure");

  r = cd_hashmap_insert(&strings->map,
                        *res,
                        len,
                        (void*) (intptr_t) cd_list_len(&strings->list));
  if (r != 0)
    return cd_error_str(kCDErrNoMem, "cd_list_push strings.list failure");

  return cd_ok();
}


void cd_strings_print(cd_strings_t* strings, int fd) {
  unsigned int i;
  unsigned int len;

  len = cd_list_len(&strings->list);
  for (i = 0; i < len; i++) {
    void* str;
    cd_list_get(&strings->list, i, &str);
    if (i == len - 1)
      dprintf(fd, "\"%s\"", str);
    else
      dprintf(fd, "\"%s\", ", str);
  }
}
