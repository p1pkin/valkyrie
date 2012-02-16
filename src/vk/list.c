
#include "vk/list.h"

vk_list *
vk_list_new (void)
{
	vk_list *list = CALLOC (vk_node);
	if (!list)
		return NULL;
	list->next = list;
	list->prev = list;
	list->key = &root_token;
	return list;
}

void
vk_list_delete (vk_list **list_)
{
	if (list_) {
		/* TODO */
		*list_ = NULL;
	}
}

void
vk_list_delete_deep (vk_list **list_)
{
	if (list_) {
		/* TODO */
		*list_ = NULL;
	}
}

bool
vk_list_is_empty (vk_list *list)
{
	return list->prev == list && list->next == list;
}

vk_list *
vk_list_append (vk_list *list)
{

}

vk_list *
vk_list_insert (vk_list *list, unsigned index)
{

}

vk_list *
vk_list_get (vk_list *list, unsigned index)
{

}

unsigned
vk_list_get_length (vk_list *list)
{
}

