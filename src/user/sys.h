#ifndef USER_SYS_H
#define USER_SYS_H

#include "syscall_arch.h"
#include "syscall_num.h"

static inline long helloworld(void)
{
  return __syscall0(SYS_helloworld);
}

static inline int copyin(const void *src, unsigned long count)
{
  return (int)__syscall2(SYS_copyin, (long)src, (long)count);
}

static inline int copyout(void *dst)
{
  return (int)__syscall1(SYS_copyout, (long)dst);
}

static inline int copyinstr(const char *src)
{
  return (int)__syscall1(SYS_copyinstr, (long)src);
}

#define SYSCALL0(n) __syscall0((long)(n))
#define SYSCALL1(n, a) __syscall1((long)(n), (long)(a))
#define SYSCALL2(n, a, b) __syscall2((long)(n), (long)(a), (long)(b))
#define SYSCALL3(n, a, b, c) \
  __syscall3((long)(n), (long)(a), (long)(b), (long)(c))
#define SYSCALL4(n, a, b, c, d) \
  __syscall4((long)(n), (long)(a), (long)(b), (long)(c), (long)(d))
#define SYSCALL5(n, a, b, c, d, e) \
  __syscall5((long)(n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e))
#define SYSCALL6(n, a, b, c, d, e, f) \
  __syscall6((long)(n), (long)(a), (long)(b), (long)(c), (long)(d), \
             (long)(e), (long)(f))
#define SYSCALL_PICK(_0, _1, _2, _3, _4, _5, _6, NAME, ...) NAME
#define syscall(...) \
  SYSCALL_PICK(__VA_ARGS__, SYSCALL6, SYSCALL5, SYSCALL4, \
               SYSCALL3, SYSCALL2, SYSCALL1, SYSCALL0)(__VA_ARGS__)

static inline long brk(unsigned long top)
{
  return __syscall1(SYS_brk, (long)top);
}

static inline unsigned long mmap(void *start, unsigned long len)
{
  return (unsigned long)__syscall2(SYS_mmap, (long)start, (long)len);
}

static inline int munmap(void *start, unsigned long len)
{
  return (int)__syscall2(SYS_munmap, (long)start, (long)len);
}

static inline long user_printf(const char *s)
{
  return __syscall1(SYS_printf, (long)s);
}

#endif
