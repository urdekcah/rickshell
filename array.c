#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "array.h"
#include "memory.h"

static void array_grow(array* a) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  size_t new_capacity = a->capacity == 0 ? 1 : a->capacity * 2;
  a->data = rrealloc(a->data, new_capacity * a->element_size);
  a->capacity = new_capacity;
}

array create_array(size_t element_size) {
  array a;
  a.data = rcalloc(1, element_size);
  a.size = 0;
  a.capacity = 1;
  a.element_size = element_size;
  a.flags = 0;
  return a;
}

array create_array_with_first_element(size_t element_size, void* fe) {
  array a = create_array(element_size);
  array_push(&a, fe);
  return a;
}

array create_array_with_capacity(size_t element_size, size_t capacity) {
  array a;
  a.data = rcalloc(capacity, element_size);
  a.size = 0;
  a.capacity = capacity;
  a.element_size = element_size;
  a.flags = 0;
  return a;
}

void* array_get_first(array a) {
  return a.size > 0 ? a.data : NULL;
}

void* array_get_last(array a) {
  return a.size > 0 ? (char*)a.data + (a.size - 1) * a.element_size : NULL;
}

void* array_get(array a, size_t index) {
  return index < a.size ? (char*)a.data + index * a.element_size : NULL;
}

void* array_checked_get(array a, size_t index) {
  assert(index < a.size && "Index out of bounds");
  return array_get(a, index);
}

void array_delete_first(array* a) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (a->size > 0) {
    memmove(a->data, (char*)a->data + a->element_size, (a->size - 1) * a->element_size);
    a->size--;
    memset((char*)a->data + a->size * a->element_size, 0, a->element_size);
  }
}

void array_delete_last(array* a) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (a->size > 0) {
    a->size--;
    memset((char*)a->data + a->size * a->element_size, 0, a->element_size);
  }
}

void array_trim(array* a, size_t index) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (index < a->size) {
    memset((char*)a->data + index * a->element_size, 0, (a->size - index) * a->element_size);
    a->size = index;
  }
}

void array_drop_item(array* a, size_t num) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (num > a->size) {
    num = a->size;
  }
  memmove(a->data, (char*)a->data + num * a->element_size, (a->size - num) * a->element_size);
  memset((char*)a->data + (a->size - num) * a->element_size, 0, num * a->element_size);
  a->size -= num;
}

void array_drop_item_index(array* a, size_t index, size_t num) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (index >= a->size) return;
  if (index + num > a->size) {
    num = a->size - index;
  }
  memmove((char*)a->data + index * a->element_size, 
          (char*)a->data + (index + num) * a->element_size, 
          (a->size - index - num) * a->element_size);
  memset((char*)a->data + (a->size - num) * a->element_size, 0, num * a->element_size);
  a->size -= num;
}

void array_clone(array* dest, array* src) {
  dest->element_size = src->element_size;
  dest->size = src->size;
  dest->capacity = src->capacity;
  dest->flags = src->flags;
  dest->data = malloc(src->capacity * src->element_size);
  if (dest->data == NULL) {
    assert(0 && "Memory allocation failed");
    return;
  }
  memcpy(dest->data, src->data, src->size * src->element_size);
}

void array_index_set(array* a, size_t index, void* value) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  assert(index < a->size && "Index out of bounds");
  memcpy((char*)a->data + index * a->element_size, value, a->element_size);
}

void array_push(array* a, void* value) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  if (a->size == a->capacity) array_grow(a);
  memcpy((char*)a->data + a->size * a->element_size, value, a->element_size);
  a->size++;
}

void array_push_repeat(array* a, void* value, size_t num) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  for (size_t i = 0; i < num; i++)
    array_push(a, value);
}

void array_push_repeat_index(array* a, size_t index, void* value, size_t num) {
  if (array_has_flag(a, ArrayFlag_Fixed)) return;
  assert(index <= a->size && "Index out of bounds");
  size_t new_size = a->size + num;
  while (new_size > a->capacity) array_grow(a);
  memmove((char*)a->data + (index + num) * a->element_size,
          (char*)a->data + index * a->element_size,
          (a->size - index) * a->element_size);
  for (size_t i = 0; i < num; i++) {
    memcpy((char*)a->data + (index + i) * a->element_size, value, a->element_size);
  }
  a->size = new_size;
}

array array_slice(array* a, size_t start, size_t end) {
  assert(start <= end && end <= a->size && "Invalid slice range");
  array slice = create_array(a->element_size);
  size_t slice_size = end - start;
  slice.data = malloc(slice_size * a->element_size);
  if (slice.data == NULL) {
    assert(0 && "Memory allocation failed");
    return slice;
  }
  memcpy(slice.data, (char*)a->data + start * a->element_size, slice_size * a->element_size);
  slice.size = slice_size;
  slice.capacity = slice_size;
  return slice;
}

void array_free(array* a) {
  if (a->data) {
    memset(a->data, 0, a->capacity * a->element_size);
    free(a->data);
    a->data = NULL;
  }
  a->size = 0;
  a->capacity = 0;
}

void array_set_flag(array* a, ArrayFlag flag) { a->flags |= flag; }
void array_clear_flag(array* a, ArrayFlag flag) { a->flags &= ~flag; }
int array_has_flag(array* a, ArrayFlag flag) { return (a->flags & flag) != 0; }
void array_toggle_flag(array* a, ArrayFlag flag) { a->flags ^= flag; }