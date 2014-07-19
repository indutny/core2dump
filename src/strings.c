#include "strings.h"
#include "common.h"
#include "error.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void cd_strings_print_json(cd_strings_t* strings,
                                  int fd,
                                  cd_strings_item_t* item);

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

  r = cd_hashmap_insert(&strings->map, str, len, item);
  if (r != 0) {
    free(item);
    return cd_error_str(kCDErrNoMem, "hashmap insert strings.map failure");
  }

  item->str[len] = '\0';
  item->len = len;
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
    cd_strings_print_json(strings, fd, item);
    if (q != QUEUE_PREV(&strings->queue))
      dprintf(fd, ", ");
  }
}


void cd_strings_print_json(cd_strings_t* strings,
                           int fd,
                           cd_strings_item_t* item) {
  int i;
  int size;
  static char storage[1024];
  char* str;
  char* ptr;

  /* Calculate string size */
  size = 0;
  for (i = 0; i < item->len; i++) {
    unsigned char c;

    c = (unsigned char) item->str[i];
    /* Two-byte char, encode as \uXXXX */
    if ((c & 0xe0) == 0xc0) {
      size += 5;

    /* \" \\ \/ \b \f \r \n \t */
    } else if (c == '"' || c == '\\' || c == '/' || c == 8 || c == 12 ||
               c == 10 || c == 13 || c == 9) {
      size += 2;
    } else {
      size++;
    }
  }

  /* Allocate enough space */
  if (size > (int) sizeof(storage))
    str = malloc(size + 1);
  else
    str = storage;

  /* Encode string */
  for (ptr = str, i = 0; i < item->len; i++) {
    unsigned char c;

    c = (unsigned char) item->str[i];
    /* Two-byte char, encode as \uXXXX */
    if ((c & 0xe0) == 0xc0) {
      unsigned char s;

      *(ptr++) = '\\';
      *(ptr++) = 'u';

      if (i == item->len - 1)
        s = 0;
      else
        s = (unsigned char) item->str[i++];

      ptr += sprintf(ptr, "%.2x%.2x", c, s);

    /* \" \\ \/ \b \f \r \n \t */
    } else if (c == '"' || c == '\\' || c == '/' || c == 8 || c == 12 ||
               c == 10 || c == 13 || c == 9) {
      *(ptr++) = '\\';
      if (c == 8)
        *(ptr++) = 'b';
      else if (c == 12)
        *(ptr++) = 'f';
      else if (c == 10)
        *(ptr++) = 'r';
      else if (c == 13)
        *(ptr++) = 'n';
      else if (c == 9)
        *(ptr++) = 't';
      else
        *(ptr++) = c;
    } else {
      *(ptr++) = c;
    }
  }

  dprintf(fd, "\"%.*s\"", size, str);

  if (str != storage)
    free(str);
}
