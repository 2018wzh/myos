#include "method.h"

void memset(void *begin, uint8 data, uint32 n)
{
  uint8 *p = begin;
  while (n-- != 0)
    *p++ = data;
}

void memmove(void *dst, const void *src, uint32 n)
{
  uint8 *d = dst;
  const uint8 *s = src;

  if (n == 0 || d == s)
    return;
  if ((uint64)d < (uint64)s) {
    while (n-- != 0)
      *d++ = *s++;
  } else {
    d += n;
    s += n;
    while (n-- != 0)
      *--d = *--s;
  }
}

int strncmp(const char *p, const char *q, uint32 n)
{
  while (n != 0 && *p != '\0' && *p == *q) {
    ++p;
    ++q;
    --n;
  }
  if (n == 0)
    return 0;
  return (int)(uint8)*p - (int)(uint8)*q;
}
