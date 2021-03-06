#ifndef SRC_STRINGS_H_
#define SRC_STRINGS_H_

#include "common.h"
#include "error.h"
#include "queue.h"

typedef struct cd_strings_s cd_strings_t;
typedef struct cd_strings_item_s cd_strings_item_t;

struct cd_strings_s {
  QUEUE queue;
  cd_hashmap_t map;
  int count;
};

struct cd_strings_item_s {
  QUEUE member;

  int index;
  int len;
  char str[1];
};

cd_error_t cd_strings_init(cd_strings_t* strings);
void cd_strings_destroy(cd_strings_t* strings);

cd_error_t cd_strings_copy(cd_strings_t* strings,
                           const char** res,
                           int* index,
                           const char* str,
                           int len);
cd_error_t cd_strings_concat(cd_strings_t* strings,
                             const char** res,
                             int* index,
                             const char* left,
                             int left_len,
                             const char* right,
                             int right_len);
void cd_strings_print(cd_strings_t* strings, cd_writebuf_t* buf);

#endif  /* SRC_STRINGS_H_ */
