#ifndef SRC_COMMON_H
#define SRC_COMMON_H

#include <stdint.h>
#include <stddef.h>

typedef struct cd_hashmap_s cd_hashmap_t;
typedef struct cd_hashmap_item_s cd_hashmap_item_t;
typedef struct cd_writebuf_s cd_writebuf_t;
typedef struct cd_splay_s cd_splay_t;
typedef int (*cd_splay_sort_cb)(const void*, const void*);
typedef struct cd_splay_node_s cd_splay_node_t;

struct cd_hashmap_item_s {
  const char* key;
  unsigned int key_len;
  void* value;
};

struct cd_hashmap_s {
  unsigned int count;
  cd_hashmap_item_t* items;

  /* If true - all keys are just raw pointers */
  int ptr;
};

struct cd_writebuf_s {
  int fd;
  unsigned int off;
  unsigned int size;

  char* buf;
};

struct cd_splay_s {
  cd_splay_node_t* root;
  cd_splay_sort_cb sort_cb;
  int allocated;
};

struct cd_splay_node_s {
  cd_splay_node_t* left;
  cd_splay_node_t* right;

  void* value;
};

uint32_t cd_jenkins(const char* str, unsigned int len);

int cd_hashmap_init(cd_hashmap_t* map, unsigned int count, int ptr);
void cd_hashmap_destroy(cd_hashmap_t* map);

int cd_hashmap_insert(cd_hashmap_t* map,
                      const char* key,
                      unsigned int key_len,
                      void* value);
void* cd_hashmap_get(cd_hashmap_t* map,
                     const char* key,
                     unsigned int key_len);
void cd_hashmap_delete(cd_hashmap_t* map,
                       const char* key,
                       unsigned int key_len);

int cd_writebuf_init(cd_writebuf_t* buf, int fd, unsigned int size);
void cd_writebuf_destroy(cd_writebuf_t* buf);

int cd_writebuf_put(cd_writebuf_t* buf, char* fmt, ...);
void cd_writebuf_flush(cd_writebuf_t* buf);

void cd_splay_init(cd_splay_t* splay, cd_splay_sort_cb sort_cb);
void cd_splay_destroy(cd_splay_t* splay);

int cd_splay_insert(cd_splay_t* splay, void* val);
void* cd_splay_find(cd_splay_t* splay, void* val);

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member) \
    ((type *) ((char *) (ptr) - offsetof(type, member)))

#endif  /* SRC_COMMON_H */
