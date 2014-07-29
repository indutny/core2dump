#include "strings.h"
#include "common.h"
#include "error.h"
#include "queue.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static const int kCDStringsInitialSize = 65536;


static void cd_strings_print_json(cd_strings_t* strings,
                                  cd_writebuf_t* buf,
                                  cd_strings_item_t* item);

cd_error_t cd_strings_init(cd_strings_t* strings) {
  QUEUE_INIT(&strings->queue);

  if (cd_hashmap_init(&strings->map, kCDStringsInitialSize, 0) != 0)
    return cd_error_str(kCDErrNoMem, "cd_hashmap_t strings");

  strings->count = 0;

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
    if (index != NULL)
      *index = item->index;
    if (res != NULL)
      *res = item->str;

    /* Return existing string */
    return cd_ok();
  }

  /* Duplicate string and insert into the list and hashmap */
  item = malloc(sizeof(*item) + len + 1);
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

  if (res != NULL)
    *res = item->str;
  if (index != NULL)
    *index = item->index;

  return cd_ok();
}


void cd_strings_print(cd_strings_t* strings, cd_writebuf_t* buf) {
  QUEUE* q;

  QUEUE_FOREACH(q, &strings->queue) {
    cd_strings_item_t* item;

    item = container_of(q, cd_strings_item_t, member);
    cd_strings_print_json(strings, buf, item);
    if (q != QUEUE_PREV(&strings->queue))
      cd_writebuf_put(buf, ", ");
  }
}


void cd_strings_print_json(cd_strings_t* strings,
                           cd_writebuf_t* buf,
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
    /* \" \\ \/ \b \f \r \n \t */
    if (c == '"' || c == '\\' || c == '/' || c == 8 || c == 9 ||
        c == 10 || c == 12 || c == 13) {
      size += 2;

    /* Two-byte char, encode as \uXXXX */
    } else if ((c & 0xe0) == 0xc0 || c < 32) {
      if (c < 32 || i == item->len - 1)
        size += 6;
      else
        size += 5;
    } else {
      size++;
    }
  }

  /* Allocate enough space */
  if (size + 1 > (int) sizeof(storage))
    str = malloc(size + 1);
  else
    str = storage;

  /* Encode string */
  for (ptr = str, i = 0; i < item->len; i++) {
    unsigned char c;

    c = (unsigned char) item->str[i];
    /* \" \\ \/ \b \f \r \n \t */
    if (c == '"' || c == '\\' || c == '/' || c == 8 || c == 9 ||
        c == 10 || c == 12 || c == 13) {
      *(ptr++) = '\\';
      if (c == 8)
        *(ptr++) = 'b';
      else if (c == 9)
        *(ptr++) = 't';
      else if (c == 10)
        *(ptr++) = 'n';
      else if (c == 12)
        *(ptr++) = 'f';
      else if (c == 13)
        *(ptr++) = 'r';
      else
        *(ptr++) = c;
    /* Two-byte char, encode as \uXXXX */
    } else if ((c & 0xe0) == 0xc0 || c < 32) {
      unsigned char s;

      *(ptr++) = '\\';
      *(ptr++) = 'u';

      if (c < 32) {
        ptr += sprintf(ptr, "00%02x", c);
      } else {
        if (i == item->len - 1)
          s = 0;
        else
          s = (unsigned char) item->str[i++];
        ptr += sprintf(ptr, "%02x%02x", c, s);
      }

    } else {
      *(ptr++) = c;
    }
  }

  cd_writebuf_put(buf, "\"%.*s\"", size, str);

  if (str != storage)
    free(str);
}
