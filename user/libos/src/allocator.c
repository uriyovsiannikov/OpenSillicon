#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "internal.h"
#define PAGE 4096UL
#define MAGIC 0x4c49424f53414c4cUL
struct __attribute__((aligned(16))) header { uint64_t magic, pages, size; };
static int add_overflow(size_t a,size_t b,size_t*out){if(a>(size_t)-1-b)return 1;*out=a+b;return 0;}
void *malloc(size_t n){size_t total,pages;struct header*h;if(!n)n=1;if(add_overflow(n,sizeof(*h),&total)||add_overflow(total,PAGE-1,&total)){errno=ENOMEM;return 0;}pages=total/PAGE;if(pages>(size_t)-1/PAGE){errno=ENOMEM;return 0;}h=__libos_mmap(pages*PAGE);if(!h)return 0;h->magic=MAGIC;h->pages=pages;h->size=n;return h+1;}
void free(void*p){struct header*h;if(!p)return;h=(struct header*)p-1;if(h->magic!=MAGIC)return;h->magic=0;__libos_free_pages(h,h->pages*PAGE);}
void *calloc(size_t n,size_t s){size_t z;void*p;if(s&&n>(size_t)-1/s){errno=ENOMEM;return 0;}z=n*s;p=malloc(z);if(p)memset(p,0,z);return p;}
void *realloc(void*p,size_t n){struct header*h;void*q;size_t copy;if(!p)return malloc(n);if(!n){free(p);return 0;}h=(struct header*)p-1;if(h->magic!=MAGIC)return 0;q=malloc(n);if(!q)return 0;copy=h->size<n?h->size:n;memcpy(q,p,copy);free(p);return q;}
