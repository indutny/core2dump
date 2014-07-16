#include <errno.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <stdio.h>
#include <unistd.h>

#include "obj.h"


cd_error_t cd_pread(int fd, void* buf, size_t nbyte, off_t offset, int* read) {
  int r;

  do
    r = pread(fd, buf, nbyte, offset);
  while (r == -1 && errno == EINTR);

  if (r < 0)
    return cd_error_num(kCDErrPread, errno);
  else if (read == NULL && r < (int) nbyte)
    return cd_error_num(kCDErrPreadNotEnough, r);

  if (read != NULL)
    *read = r;

  return cd_ok();
}
