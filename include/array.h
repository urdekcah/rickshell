#ifndef __RICKSHELL_ARRAY_H__
#define __RICKSHELL_ARRAY_H__
#include <stddef.h>

typedef enum {
  ArrayFlag_Fixed = 1U << 0,
} ArrayFlag;

typedef struct {
  void* data;
  size_t size;
  size_t capacity;
  size_t element_size;
  ArrayFlag flags;
} array;

typedef array StringArray;

void array_set_flag(array* a, ArrayFlag flag);
void array_clear_flag(array* a, ArrayFlag flag);
int array_has_flag(array* a, ArrayFlag flag);
void array_toggle_flag(array* a, ArrayFlag flag);

array create_array(size_t element_size);
array create_array_with_first_element(size_t element_size, void* fe);
array create_array_with_capacity(size_t element_size, size_t capacity);
array array_clone_from(array a);
void* array_get_first(array a);
void* array_get_last(array a);
void* array_get(array a, size_t index);
void* array_checked_get(array a, size_t index);
void array_delete_first(array* a);
void array_delete_last(array* a);
void array_trim(array* a, size_t index);
void array_drop_item(array* a, size_t num);
void array_drop_item_index(array* a, size_t index, size_t num);
void array_clone(array* dest, array* src);
void array_index_set(array* a, size_t index, void* value);
void array_push(array* a, void* value);
void array_push_repeat(array* a, void* value, size_t num);
void array_push_repeat_index(array* a, size_t index, void* value, size_t num);
array array_slice(array* a, size_t start, size_t end);
void array_free(array* a);
#endif