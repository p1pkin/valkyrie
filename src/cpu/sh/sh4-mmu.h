
#ifndef __VK_SH4_MMU_H__
#define __VK_SH4_MMU_H__

#define IS_STORE_QUEUE(addr_) \
	(addr >= 0xE0000000 && addr <= 0xE3FFFFFF)

#define IS_ON_CHIP(addr_) \
	(((addr >> 24) == 0x1F) || \
	 ((addr >> 24) == 0xFF))

#define ADDR_MASK 0x1FFFFFFF

#define AREA(addr_) \
	(((addr_) >> 26) & 7)

#endif /* __VK_SH4_MMU_H__ */
