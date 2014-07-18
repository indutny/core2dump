#ifndef SRC_STRINGS_H_
#define SRC_STRINGS_H_

#include "common.h"
#include "error.h"

typedef struct cd_strings_s cd_strings_t;

struct cd_strings_s {
  cd_list_t list;
  cd_hashmap_t map;
};

cd_error_t cd_strings_init(cd_strings_t* strings);
void cd_strings_destroy(cd_strings_t* strings);

cd_error_t cd_strings_copy(cd_strings_t* strings,
                           const char** res,
                           const char* str,
                           int len);

#endif  /* SRC_STRINGS_H_ */
