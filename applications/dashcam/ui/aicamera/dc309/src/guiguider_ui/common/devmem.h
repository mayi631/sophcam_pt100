#ifndef _DEVMEM_H_
#define _DEVMEM_H_

#include <stdint.h>
uint32_t devmem_readl(unsigned long addr);
void devmem_writel(unsigned long addr, uint32_t val);

#endif
