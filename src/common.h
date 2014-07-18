#ifndef SRC_COMMON_H
#define SRC_COMMON_H

#include <stdint.h>
#include <stddef.h>

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
  cd_hashmap_item_t* items;
};

struct cd_list_s {
  unsigned int off;
  unsigned int size;
  char* items;
  unsigned int item_size;
};

uint32_t cd_jenkins(const char* str, unsigned int len);

int cd_hashmap_init(cd_hashmap_t* map, unsigned int count);
void cd_hashmap_destroy(cd_hashmap_t* map);

int cd_hashmap_insert(cd_hashmap_t* map,
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
int cd_list_pop(cd_list_t* list, void* res);
int cd_list_get(cd_list_t* list, unsigned int index, void* res);
unsigned int cd_list_len(cd_list_t* list);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member) \
    ((type *) ((char *) (ptr) - offsetof(type, member)))

#endif  /* SRC_COMMON_H */
