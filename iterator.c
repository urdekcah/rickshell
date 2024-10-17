#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "iterator.h"

MapIterator map_iterator(const map* m) {
  MapIterator it = {m, 0, 0};
  return it;
}

bool map_has_next(MapIterator* it) {
  if (!it || !it->m) return false;
  
  while (it->current_index < it->m->capacity) {
    if (it->m->buckets[it->current_index].is_occupied) {
      return true;
    }
    it->current_index++;
  }
  return false;
}

const char* map_next(MapIterator* it) {
  if (!it || !it->m) return NULL;
  
  while (it->current_index < it->m->capacity) {
    if (it->m->buckets[it->current_index].is_occupied) {
      const char* key = it->m->buckets[it->current_index].key;
      it->current_index++;
      it->items_returned++;
      return key;
    }
    it->current_index++;
  }
  return NULL;
}

void* map_iterator_get_value(MapIterator* it, size_t* out_value_size) {
  if (!it || !it->m || it->current_index == 0 || it->current_index > it->m->capacity) {
    if (out_value_size) *out_value_size = 0;
    return NULL;
  }
  
  size_t index = it->current_index - 1;
  if (it->m->buckets[index].is_occupied) {
    if (out_value_size) *out_value_size = it->m->buckets[index].value_size;
    return it->m->buckets[index].value;
  }
  
  if (out_value_size) *out_value_size = 0;
  return NULL;
}

MapResult map_iterator_peek(MapIterator* it, const char** out_key, void** out_value, size_t* out_value_size) {
  if (!it || !it->m) return Err((void*)MAP_ERROR_INVALID_ARGUMENT);
  
  size_t current = it->current_index;
  while (current < it->m->capacity) {
    if (it->m->buckets[current].is_occupied) {
      if (out_key) *out_key = it->m->buckets[current].key;
      if (out_value) *out_value = it->m->buckets[current].value;
      if (out_value_size) *out_value_size = it->m->buckets[current].value_size;
      return Ok((void*)MAP_OK);
    }
    current++;
  }
  return Err((void*)MAP_ERROR_KEY_NOT_FOUND);
}

void map_iterator_reset(MapIterator* it) {
  if (it) {
    it->current_index = 0;
    it->items_returned = 0;
  }
}