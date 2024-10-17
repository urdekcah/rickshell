#ifndef __RICKSHELL_ITERATOR_H__
#define __RICKSHELL_ITERATOR_H__
#include "map.h"

typedef struct {
  const map* m;
  size_t current_index;
  size_t items_returned;
} MapIterator;

MapIterator map_iterator(const map* m);
bool map_has_next(MapIterator* it);
const char* map_next(MapIterator* it);
void* map_iterator_get_value(MapIterator* it, size_t* out_value_size);
MapResult map_iterator_peek(MapIterator* it, const char** out_key, void** out_value, size_t* out_value_size);
void map_iterator_reset(MapIterator* it);
#endif /* __RICKSHELL_ITERATOR_H__ */