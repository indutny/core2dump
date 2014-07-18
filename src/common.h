#ifndef SRC_COMMON_H
#define SRC_COMMON_H

#include <stdint.h>

typedef struct cd_hashmap_s cd_hashmap_t;
typedef struct cd_hashmap_item_s cd_hashmap_item_t;
typedef struct cd_list_s cd_list_t;

struct cd_hashmap_item_s {
  const char* key;
  unsigned int key_len;
  void* value;
};

struct cd_hashmap_s {
  unsigned int count;
  cd_hashmap_item_t items[1];
};

struct cd_list_s {
  unsigned int off;
  unsigned int size;
  char* items;
  unsigned int item_size;
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

int cd_list_init(cd_list_t* list, unsigned int size, unsigned int item_size);
void cd_list_free(cd_list_t* list);

int cd_list_push(cd_list_t* list, void* value);
int cd_list_shift(cd_list_t* list, void* res);
unsigned int cd_list_len(cd_list_t* list);

#endif  /* SRC_COMMON_H */
