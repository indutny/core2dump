#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "common.h"


static const int kCDHashmapMaxSkip = 24;
static const int kCDHashmapGrowRate = 512;
static const int kCDListGrowRate = 256;


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


int cd_list_init(cd_list_t* list, unsigned int size, unsigned int item_size) {
  list->items = malloc(sizeof(*list->items) * size * item_size);
  list->size = size;
  list->off = 0;
  list->item_size = item_size;

  return list->items == NULL ? -1 : 0;
}


void cd_list_free(cd_list_t* list) {
  free(list->items);
  list->items = NULL;
}


int cd_list_push(cd_list_t* list, void* value) {
  /* Realloc */
  if (list->off == list->size) {
    char* items;

    items = malloc(
          sizeof(*items) * (list->size + kCDListGrowRate) * list->item_size);
    if (items == NULL)
      return -1;

    memcpy(items, list->items, sizeof(*items) * list->size * list->item_size);
    free(list->items);
    list->items = items;
  }

  /* Push item on list */
  memcpy(list->items + list->off++ * list->item_size,
         value,
         list->item_size);

  return 0;
}


int cd_list_shift(cd_list_t* list, void* res) {
  if (list->off == 0) {
    memset(res, 0, list->item_size);
    return -1;
  }

  /* Copy-out the result */
  memcpy(res, list->items, list->item_size);

  list->off--;
  memmove(list->items,
          list->items + list->item_size,
          list->off * sizeof(*list->items) * list->item_size);

  return 0;
}


int cd_list_pop(cd_list_t* list, void* res) {
  if (list->off == 0) {
    memset(res, 0, list->item_size);
    return -1;
  }

  /* Copy-out the result */
  memcpy(res, &list->items[list->off--], list->item_size);

  return 0;
}


unsigned int cd_list_len(cd_list_t* list) {
  return list->off;
}
