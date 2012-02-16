
#ifndef __VK_LIST_H__
#define __VK_LIST_H__

typedef struct vk_node vk_list;
typedef struct vk_node vk_node;

static const uint32_t root_token = 0xDEADF00D;

struct {
	vk_node *prev;
	vk_node *next;
	void *key;
} vk_node;

#define VK_LIST_FOREACH(list, iter)

vk_list	*vk_list_new (void);

#endif /* __VK_LIST_H__ */

