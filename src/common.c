#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "common.h"


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
