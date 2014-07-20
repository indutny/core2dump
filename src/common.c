#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"


static const int kCDHashmapMaxSkip = 7;
static const int kCDHashmapGrowRate = 1024;


uint32_t cd_jenkins(const char* str, unsigned int len) {
  uint32_t hash;
  unsigned int i;

  hash = 0;
  for (i = 0; i < len; i++) {
    unsigned char ch;

    ch = (unsigned char) str[i];
    hash += ch;
    hash += hash << 10;
    hash ^= hash >> 6;
  }

  hash += hash << 3;
  hash ^= hash >> 11;
  hash += hash << 15;

  return hash;
}


int cd_hashmap_init(cd_hashmap_t* map, unsigned int count) {
  map->items = calloc(sizeof(*map->items), count);
  if (map->items == NULL)
    return -1;

  map->count = count;

  return 0;
}


void cd_hashmap_destroy(cd_hashmap_t* map) {
  free(map->items);
  map->items = NULL;
}


int cd_hashmap_insert(cd_hashmap_t* map,
                      const char* key,
                      unsigned int key_len,
                      void* value) {
  uint32_t index;

  do {
    int skip;

    index = cd_jenkins(key, key_len) % map->count;
    for (skip = 0;
         skip < kCDHashmapMaxSkip && map->items[index].key != NULL;
         skip++) {
      /* Equal entries */
      if (map->items[index].key_len == key_len &&
          strncmp(map->items[index].key, key, key_len) == 0) {
        return 0;
      }
      index = (index + 1) % map->count;
    }

    if (skip != kCDHashmapMaxSkip)
      break;

    /* Grow is needed */
    cd_hashmap_item_t* items;
    cd_hashmap_item_t* nitems;
    unsigned int i;
    unsigned int count;

    nitems = calloc(sizeof(*nitems), map->count + kCDHashmapGrowRate);
    if (nitems == NULL)
      return -1;

    /* Rehash */
    items = map->items;
    count = map->count;

    map->count += kCDHashmapGrowRate;
    map->items = nitems;
    for (i = 0; i < count; i++) {
      if (items[i].key == NULL)
        continue;
      cd_hashmap_insert(map,
                        items[i].key,
                        items[i].key_len,
                        items[i].value);
    }
    free(items);

    /* Retry inserting */
    continue;
  } while (1);

  map->items[index].key = key;
  map->items[index].key_len = key_len;
  map->items[index].value = value;

  return 0;
}


void* cd_hashmap_get(cd_hashmap_t* map,
                     const char* key,
                     unsigned int key_len) {
  uint32_t index;

  index = cd_jenkins(key, key_len) % map->count;
  do {
    /* Not found */
    if (map->items[index].key == NULL)
      return NULL;

    if (key_len == map->items[index].key_len &&
        strncmp(map->items[index].key, key, key_len) == 0) {
      return map->items[index].value;
    }

    /* Move forward */
    index = (index + 1) % map->count;
  } while (1);
}


int cd_writebuf_init(cd_writebuf_t* buf, int fd, unsigned int size) {
  buf->fd = fd;
  buf->off = 0;
  buf->size = size;
  /* Trailing zero in snprintf */
  buf->buf = malloc(size + 1);
  if (buf->buf == NULL)
    return -1;

  return 0;
}


void cd_writebuf_destroy(cd_writebuf_t* buf) {
  free(buf->buf);
  buf->buf = NULL;
}

int cd_writebuf_put(cd_writebuf_t* buf, char* fmt, ...) {
  va_list ap_orig;
  va_list ap;
  int r;

  va_start(ap_orig, fmt);

  do {
    /* Copy the vararg to retry writing in case of failure */
    va_copy(ap, ap_orig);

    r = vsnprintf(buf->buf + buf->off, buf->size - buf->off + 1, fmt, ap);
    assert(r >= 0);

    /* Whole string was written */
    if ((unsigned int) r <= buf->size - buf->off)
      break;

    /* Free the buffer and try again */
    cd_writebuf_flush(buf);

    /* Realloc is needed */
    if ((unsigned int) r > buf->size) {
      char* tmp;

      /* Trailing zero */
      tmp = malloc(r + 1);
      if (tmp == NULL)
        return -1;

      free(buf->buf);
      buf->buf = tmp;
      buf->size = r;
      /* NOTE: buf->off should already be 0 */
    }

    /* Retry */
    va_end(ap);
  } while (1);

  /* Success */
  va_end(ap);
  va_end(ap_orig);

  buf->off += r;
  if (buf->off == buf->size)
    cd_writebuf_flush(buf);

  return 0;
}


void cd_writebuf_flush(cd_writebuf_t* buf) {
  dprintf(buf->fd, "%.*s", buf->off, buf->buf);
  buf->off = 0;
}
