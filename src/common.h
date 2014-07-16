#ifndef SRC_COMMON_H
#define SRC_COMMON_H

#include <stdint.h>

typedef struct cd_hashmap_s cd_hashmap_t;
typedef struct cd_hashmap_item_s cd_hashmap_item_t;

struct cd_hashmap_item_s {
  const char* key;
  unsigned int key_len;
  void* value;
};

struct cd_hashmap_s {
  unsigned int count;
  cd_hashmap_item_t items[1];
};

uint32_t cd_jenkins(const char* str, unsigned int len);

cd_hashmap_t* cd_hashmap_new(unsigned int count);
void cd_hashmap_free(cd_hashmap_t* map);

void cd_hashmap_insert(cd_hashmap_t* map,
                       const char* key,
                       unsigned int key_len,
                       void* value);
void* cd_hashmap_get(cd_hashmap_t* map,
                     const char* key,
                     unsigned int key_len);

#endif  /* SRC_COMMON_H */
