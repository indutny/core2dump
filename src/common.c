#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "common.h"


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


cd_hashmap_t* cd_hashmap_new(unsigned int count) {
  unsigned int i;
  cd_hashmap_t* res;

  res = malloc(sizeof(*res) + (count - 1) * sizeof(*res->items));
  if (res == NULL)
    return NULL;

  res->count = count;
  for (i = 0; i < count; i++)
    res->items[i].key = NULL;

  return res;
}


void cd_hashmap_free(cd_hashmap_t* map) {
  free(map);
}


void cd_hashmap_insert(cd_hashmap_t* map,
                       const char* key,
                       unsigned int key_len,
                       void* value) {
  uint32_t index;

  index = cd_jenkins(key, key_len) % map->count;
  while (map->items[index].key != NULL)
    index = (index + 1) % map->count;

  map->items[index].key = key;
  map->items[index].key_len = key_len;
  map->items[index].value = value;
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


int cd_list_init(cd_list_t* list, unsigned int size) {
  list->items = malloc(sizeof(*list->items) * size);
  list->size = size;
  list->off = 0;

  return list->items == NULL ? -1 : 0;
}


void cd_list_free(cd_list_t* list) {
  free(list->items);
  list->items = NULL;
}


int cd_list_push(cd_list_t* list, void* value) {
  /* Realloc */
  if (list->off == list->size) {
    void** items;

    items = malloc(sizeof(*items) * (list->size + kCDListGrowRate));
    if (items == NULL)
      return -1;

    memcpy(items, list->items, sizeof(*items) * list->size);
    free(list->items);
    list->items = items;
  }

  /* Push item on list */
  list->items[list->off++] = value;

  return 0;
}


void* cd_list_shift(cd_list_t* list) {
  void* r;

  if (list->off == 0)
    return NULL;

  r = list->items[0];
  list->off--;
  memmove(list->items, list->items + 1, list->off * sizeof(*list->items));

  return r;
}


unsigned int cd_list_len(cd_list_t* list) {
  return list->off;
}
