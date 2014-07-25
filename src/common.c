#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common.h"


static const int kCDHashmapMaxSkip = 64;
static const int kCDHashmapGrowRateLimit = 1048576;


static void cd_splay_destroy_rec(cd_splay_t* splay, cd_splay_node_t* node);
static void cd_splay(cd_splay_t* splay,
                     cd_splay_node_t** g,
                     cd_splay_node_t** p,
                     cd_splay_node_t** c);


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


int cd_hashmap_init(cd_hashmap_t* map, unsigned int count, int ptr) {
  map->items = calloc(count, sizeof(*map->items));
  if (map->items == NULL)
    return -1;

  map->count = count;
  map->ptr = ptr;

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
    cd_hashmap_item_t* items;
    cd_hashmap_item_t* nitems;
    unsigned int i;
    unsigned int count;
    unsigned int grow;

    if (map->ptr)
      index = cd_jenkins((const char*) &key, key_len) % map->count;
    else
      index = cd_jenkins(key, key_len) % map->count;

    for (skip = 0;
         skip < kCDHashmapMaxSkip && map->items[index].key != NULL;
         skip++) {
      cd_hashmap_item_t* item;

      item = &map->items[index];

      /* Equal entries - update */
      if (key_len == item->key_len &&
          (map->ptr ? item->key == key :
                      (memcmp(item->key, key, key_len) == 0))) {
        break;
      }

      index = (index + 1) % map->count;
    }

    if (skip != kCDHashmapMaxSkip)
      break;

    /* Grow is needed */
    if (map->count < kCDHashmapGrowRateLimit)
      grow = map->count;
    else
      grow = kCDHashmapGrowRateLimit;

    nitems = calloc(map->count + grow, sizeof(*nitems));
    if (nitems == NULL)
      return -1;

    /* Rehash */
    items = map->items;
    count = map->count;

    map->count += grow;
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

  if (map->ptr)
    index = cd_jenkins((const char*) &key, key_len) % map->count;
  else
    index = cd_jenkins(key, key_len) % map->count;

  do {
    cd_hashmap_item_t* item;

    item = &map->items[index];

    /* Not found */
    if (item->key == NULL)
      return NULL;

    if (key_len == item->key_len &&
        (map->ptr ? item->key == key :
                    (memcmp(item->key, key, key_len) == 0))) {
      return item->value;
    }

    /* Move forward */
    index = (index + 1) % map->count;
  } while (1);
}


void cd_hashmap_delete(cd_hashmap_t* map,
                       const char* key,
                       unsigned int key_len) {
  uint32_t index;

  if (map->ptr)
    index = cd_jenkins((const char*) &key, key_len) % map->count;
  else
    index = cd_jenkins(key, key_len) % map->count;

  do {
    cd_hashmap_item_t* item;

    item = &map->items[index];

    /* Not found */
    if (item->key == NULL)
      return;

    if (key_len == item->key_len &&
        (map->ptr ? item->key == key :
                    (memcmp(item->key, key, key_len) == 0))) {
      item->key = NULL;
      return;
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


void cd_splay_init(cd_splay_t* splay, cd_splay_sort_cb sort_cb) {
  splay->root = NULL;
  splay->sort_cb = sort_cb;
  splay->allocated = 0;
}


void cd_splay_destroy_rec(cd_splay_t* splay, cd_splay_node_t* node) {
  if (node == NULL)
    return;
  cd_splay_destroy_rec(splay, node->left);
  cd_splay_destroy_rec(splay, node->right);
  if (splay->allocated)
    free(node->value);
  free(node);
}


void cd_splay_destroy(cd_splay_t* splay) {
  cd_splay_destroy_rec(splay, splay->root);
}


void cd_splay(cd_splay_t* splay,
              cd_splay_node_t** g,
              cd_splay_node_t** p,
              cd_splay_node_t** c) {
  int pleft;
  int gleft;
  cd_splay_node_t* np;
  cd_splay_node_t* ng;
  cd_splay_node_t* nc;

  /* c is a root now */
  if (p == NULL || *p == NULL)
    return;

  nc = *c;
  np = *p;
  pleft = np->left == nc;

  /* p is a root: Zig Step */
  if (g == NULL || *g == NULL) {
    if (pleft) {
      np->left = nc->right;
      nc->right = np;
    } else {
      np->right = nc->left;
      nc->left = np;
    }
    *p = nc;
    return;
  }

  ng = *g;
  gleft = ng->left == np;

  /* Both p and c on the same branch: Zig-Zig Step */
  if ((pleft && gleft) || (!pleft && !gleft)) {
    if (gleft) {
      ng->left = np->right;
      np->right = ng;
      np->left = nc->right;
      nc->right = np;
    } else {
      ng->right = np->left;
      np->left = ng;
      np->right = nc->left;
      nc->left = np;
    }
    *g = nc;
    return;
  }

  /* Zig-Zag Step */
  if (gleft) {
    ng->left = nc->right;
    np->right = nc->left;
    nc->left = np;
    nc->right = ng;
  } else {
    ng->right = nc->left;
    np->left = nc->right;
    nc->right = np;
    nc->left = ng;
  }
  *g = nc;
}


int cd_splay_insert(cd_splay_t* splay, void* val) {
  /* Grand-parent */
  cd_splay_node_t** g;
  /* Parent */
  cd_splay_node_t** p;
  /* Current node */
  cd_splay_node_t** c;
  /* Next node */
  cd_splay_node_t** n;
  /* Node to insert */
  cd_splay_node_t* node;

  node = malloc(sizeof(*node));
  if (node == NULL)
    return -1;

  node->left = NULL;
  node->right = NULL;
  node->value = val;

  /* Traverse */
  for (g = NULL, p = NULL, c = &splay->root; *c != NULL; g = p, p = c, c = n) {
    int cmp;

    cmp = splay->sort_cb((*c)->value, val);
    if (cmp < 0)
      n = &(*c)->left;
    else if (cmp > 0)
      n = &(*c)->right;
    else
      break;
  }

  /* Value is already here */
  if (*c != NULL) {
    free(node);
    return -1;
  }

  /* Insert value */
  *c = node;

  cd_splay(splay, g, p, c);

  return 0;
}


void* cd_splay_find(cd_splay_t* splay, void* val) {
  /* Grand-grand parent */
  cd_splay_node_t** gg;
  /* Grand-parent */
  cd_splay_node_t** g;
  /* Parent */
  cd_splay_node_t** p;
  /* Current node */
  cd_splay_node_t** c;
  /* Next node */
  cd_splay_node_t** n;
  /* Result node */
  cd_splay_node_t* r;

  /* Traverse */
  gg = NULL;
  g = NULL;
  p = NULL;
  r = NULL;
  for (c = &splay->root; *c != NULL; gg = g, g = p, p = c, c = n) {
    int cmp;

    cmp = splay->sort_cb((*c)->value, val);
    if (cmp < 0) {
      r = *c;
      n = &r->left;
    } else if (cmp > 0) {
      n = &(*c)->right;
    } else {
      r = *c;
      break;
    }
  }

  /* Every node was smaller than `val` */
  if (r == NULL)
    return NULL;

  /* Exact match */
  if (*c != NULL) {
    cd_splay(splay, g, p, c);
    goto done;
  }

  /* Inexact match */
  cd_splay(splay, gg, g, p);

done:
  return r->value;
}
