#ifndef __RICKSHELL_MEMORY_H__
#define __RICKSHELL_MEMORY_H__
#include <stdlib.h>

void* rmalloc(size_t size);
void* rcalloc(size_t num, size_t size);
void* rrealloc(void* ptr, size_t size);
void* rrealloc_data(void* old_data, size_t old_size, size_t new_size);
void rfree(void* ptr);
char* rstrdup(const char* s);

void *unsafe_rmalloc(size_t size);
void* unsafe_rcalloc(size_t num, size_t size);
#endif /* __RICKSHELL_MEMORY_H__ */