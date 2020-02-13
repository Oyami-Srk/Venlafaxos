#ifndef __MEMORY_H__
#define __MEMORY_H__

#include "generic/typedefs.h"

#define KERN_BASE 0x80000000
#define KP2V(a)   ((void *)(((char *)(a)) + KERN_BASE))
#define KV2P(a)   ((void *)(((char *)(a)) - KERN_BASE))

#define PG_SIZE  0x1000
#define PDE_SIZE 0x1000

extern volatile char _KERN_VEND[];
#define KERN_VEND (((void *)(_KERN_VEND)))

void  core_init_memory(void *vstart, void *vend);
void  kfree(char *p);
char *kalloc(void);

#endif // __MEMORY_H__
