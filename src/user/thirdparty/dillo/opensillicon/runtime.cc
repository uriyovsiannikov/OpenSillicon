/* The port uses no exceptions, RTTI, STL or threads. */
#include "compat.h"
void *operator new(size_t n) { void *p=malloc(n?n:1); if (!p) abort(); return p; }
void *operator new[](size_t n) { return ::operator new(n); }
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }
extern "C" void __cxa_pure_virtual() { abort(); }
extern "C" {
extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);
void os_dillo_init()
{
    for (auto p=__init_array_start; p!=__init_array_end; ++p) (*p)();
    if (!getenv("HOME")) setenv("HOME","/tmp",1);
}
}
