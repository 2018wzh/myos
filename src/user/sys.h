#ifndef USER_SYS_H
#define USER_SYS_H

#include "syscall_arch.h"
#include "syscall_num.h"

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
  return __syscall2(SYS_mmap, (long)start, (long)len);
}

static inline int munmap(void *start, unsigned long len)
{
  return __syscall2(SYS_munmap, (long)start, (long)len);
}

static inline long user_printf(const char *s)
{
  return __syscall1(SYS_print_str, (long)s);
}

static inline int print_int(int value)
{
  return syscall(SYS_print_int, value);
}

static inline int getpid(void)
{
  return syscall(SYS_getpid);
}

static inline int fork(void)
{
  return syscall(SYS_fork);
}

static inline int wait(int *code)
{
  return syscall(SYS_wait, code);
}

static inline void exit(int code) __attribute__((noreturn));

static inline void exit(int code)
{
  (void)syscall(SYS_exit, code);
  __builtin_unreachable();
}

static inline int sleep(unsigned int ntick)
{
  return syscall(SYS_sleep, ntick);
}

#endif
