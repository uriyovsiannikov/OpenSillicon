#include "kernel/log.h"

int module_init(void);
void module_fini(void);

int
module_init(void)
{
	klog("hello: module loaded\n");
	return 0;
}

void
module_fini(void)
{
	klog("hello: module unloaded\n");
}
