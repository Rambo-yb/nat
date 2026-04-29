#ifndef __LIST_H__
#define __LIST_H__

#ifdef __cplusplus
extern "C" {
#endif

void* ProtListCreate();

void ProtListDestory(void* list_head);

int ProtListPush(void* list_head, void* data, int data_size);

int ProtListPopHead(void* list_head, void* data, int data_size);

int ProtListPop(void* list_head, void* data, int data_size);

int ProtListSize(void* list_head);

void* ProtListGet(void* list_head, int index);

int ProtListRemove(void* list_head, int index);

#ifdef __cplusplus
};
#endif

#endif