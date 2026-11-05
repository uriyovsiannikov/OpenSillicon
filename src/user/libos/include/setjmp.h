#ifndef LIBOS_SETJMP_H
#define LIBOS_SETJMP_H
typedef unsigned long jmp_buf[8];
typedef unsigned long sigjmp_buf[8];
int _setjmp(jmp_buf);
int __sigsetjmp(sigjmp_buf, int);
void longjmp(jmp_buf, int) __attribute__((noreturn));
void siglongjmp(sigjmp_buf, int) __attribute__((noreturn));
#define setjmp(env) _setjmp(env)
#define sigsetjmp(env, save_mask) __sigsetjmp((env), (save_mask))
#endif
