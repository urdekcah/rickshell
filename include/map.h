#ifndef __RICKSHELL_MAP_H__
#define __RICKSHELL_MAP_H__
#include <stdbool.h>
#include <stddef.h>
#include <pthread.h>
#include "result.h"

#define MAP_INITIAL_CAPACITY 2

typedef struct {
  char* key;
  void* value;
  size_t value_size;
  bool is_occupied;
} map_kv;

typedef void (*MapFreeFn)(void*);

typedef struct {
  map_kv* buckets;
  size_t capacity;
  size_t size;
  size_t num_deleted;
  pthread_mutex_t lock;
  MapFreeFn value_free;
} map;

map* create_map_with_func(MapFreeFn value_free);
map* create_map();
MapResult resize_map(map* m, size_t new_capacity);
MapResult map_insert(map* m, const char* key, void* value, size_t value_size);
MapResult map_get(const map* m, const char* key, void* out_value, size_t* out_value_size);
bool map_remove(map* m, const char* key);
void map_free(map* m);
#endif /* __RICKSHELL_MAP_H__ */