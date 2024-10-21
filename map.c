#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include "map.h"
#include "memory.h"
#include "result.h"
#include "wyhash.h"

map* create_map_with_func(MapFreeFn value_free) {
  map* m = (map*)rmalloc(sizeof(map));
  if (!m) {
    return NULL;
  }
  m->capacity = MAP_INITIAL_CAPACITY;
  m->size = 0;
  m->num_deleted = 0;
  m->buckets = (map_kv*)calloc(m->capacity, sizeof(map_kv));
  if (!m->buckets) {
    free(m);
    return NULL;
  }
  if (pthread_mutex_init(&m->lock, NULL) != 0) {
    rfree(m->buckets);
    rfree(m);
    return NULL;
  }
  m->value_free = value_free;
  return m;
}

map* create_map() {
  return create_map_with_func(rfree);
}

MapResult resize_map(map* m, size_t new_capacity) {
  if (!m || new_capacity < m->size) return Err(
    _SLIT("Invalid argument: map is NULL or new capacity is less than current size"),
    ERRCODE_INVALID_ARGUMENT
  );
  map_kv *new_buckets = (map_kv*)calloc(new_capacity, sizeof(map_kv));
  if (!new_buckets) return Err(
    _SLIT("Failed to allocate memory for new map buckets"),
    ERRCODE_MEMALLOC_FAILED
  );
  register size_t i;
  for (i = 0; i < m->capacity; i++) {
    if (m->buckets[i].is_occupied) {
      size_t index = wyhash(m->buckets[i].key, strlen(m->buckets[i].key), 0, _wyp) % new_capacity;
      while (new_buckets[index].is_occupied) {
        index = (index + 1) % new_capacity;
      }
      new_buckets[index] = m->buckets[i];
    } else if (m->buckets[i].key) {
      rfree(m->buckets[i].key);
      if (m->value_free && m->buckets[i].value)
        m->value_free(m->buckets[i].value);
    }
  }

  rfree(m->buckets);
  m->buckets = new_buckets;
  m->capacity = new_capacity;
  m->num_deleted = 0;
  return Ok(NULL);
}

MapResult map_insert(map* m, const char* key, void* value, size_t value_size) {
  if (!m || !key) return Err(
    _SLIT("Invalid argument: map or key is NULL"),
    ERRCODE_INVALID_ARGUMENT
  );
  pthread_mutex_lock(&m->lock);
  if (m->size >= m->capacity / 2) {
    MapResult result = resize_map(m, m->capacity * 2);
    if (result.is_err) {
      pthread_mutex_unlock(&m->lock);
      return result;
    }
  }

  uint64_t hash = wyhash(key, strlen(key), 0, _wyp);
  size_t index = hash % m->capacity;

  while (m->buckets[index].is_occupied) {
    if (strcmp(m->buckets[index].key, key) == 0) {
      if (m->value_free) m->value_free(m->buckets[index].value);
      void* new_value = malloc(value_size);
      if (!new_value) {
        pthread_mutex_unlock(&m->lock);
        return Err(
          _SLIT("Failed to allocate memory for new value"),
          ERRCODE_MEMALLOC_FAILED
        );
      }
      memcpy(new_value, value, value_size);
      m->buckets[index].value = new_value;
      m->buckets[index].value_size = value_size;
      pthread_mutex_unlock(&m->lock);
      return Ok(NULL);
    }
    index = (index + 1) % m->capacity;
  }

  char* new_key = strdup(key);
  void* new_value = malloc(value_size);
  if (!new_key || !new_value) {
    rfree(new_key);
    rfree(new_value);
    pthread_mutex_unlock(&m->lock);
    return Err(
      _SLIT("Failed to allocate memory for new key or value"),
      ERRCODE_MEMALLOC_FAILED
    );
  }
  memcpy(new_value, value, value_size);
  m->buckets[index].key = new_key;
  m->buckets[index].value = new_value;
  m->buckets[index].value_size = value_size;
  m->buckets[index].is_occupied = true;
  m->size++;

  pthread_mutex_unlock(&m->lock);
  return Ok(NULL);
}

MapResult map_get(const map* m, const char* key, void* out_value, size_t* out_value_size) {
  if (!m || !key || !out_value || !out_value_size) return Err(
    _SLIT("Invalid argument: map, key, out_value, or out_value_size is NULL"),
    ERRCODE_INVALID_ARGUMENT
  );
  pthread_mutex_lock((pthread_mutex_t*)&m->lock);
  uint64_t hash = wyhash(key, strlen(key), 0, _wyp);
  size_t index = hash % m->capacity;
  while (m->buckets[index].is_occupied) {
    if (strcmp(m->buckets[index].key, key) == 0) {
      *out_value_size = m->buckets[index].value_size;
      memcpy(out_value, m->buckets[index].value, m->buckets[index].value_size);
      pthread_mutex_unlock((pthread_mutex_t*)&m->lock);
      return Ok(NULL);
    }
    index = (index + 1) % m->capacity;
  }
  pthread_mutex_unlock((pthread_mutex_t*)&m->lock);
  return Err(
    _SLIT("Key not found in map"),
    ERRCODE_MAP_KEY_NOT_FOUND
  );
}

bool map_remove(map* m, const char* key) {
  if (!m || !key) return false;
  pthread_mutex_lock(&m->lock);
  uint64_t hash = wyhash(key, strlen(key), 0, _wyp);
  size_t index = hash % m->capacity;

  while (m->buckets[index].is_occupied) {
    if (strcmp(m->buckets[index].key, key) == 0) {
      free(m->buckets[index].key);
      if (m->value_free) m->value_free(m->buckets[index].value);
      else rfree(m->buckets[index].value);

      size_t next = (index + 1) % m->capacity;
      while (m->buckets[next].is_occupied) {
        uint64_t _hash = wyhash(m->buckets[next].key, strlen(m->buckets[next].key), 0, _wyp);
        size_t ideal_pos = _hash % m->capacity;
        if ((index < next && (ideal_pos <= index || ideal_pos > next)) || (index > next && (ideal_pos <= index && ideal_pos > next))) {
          m->buckets[index] = m->buckets[next];
          index = next;
        }
        next = (next + 1) % m->capacity;
      }
      memset(&m->buckets[index], 0, sizeof(map_kv));
      m->size--;
      m->num_deleted++;
      pthread_mutex_unlock(&m->lock);
      return true;
    }
    index = (index + 1) % m->capacity;
  }
  pthread_mutex_unlock(&m->lock);
  return false;
}

void map_free(map* m) {
  if (!m) return;
  pthread_mutex_lock(&m->lock);
  register size_t i;
  for (i = 0; i < m->capacity; i++) {
    if (m->buckets[i].is_occupied) {
      free(m->buckets[i].key);
      if (m->value_free) m->value_free(m->buckets[i].value);
      else rfree(m->buckets[i].value);
    }
  }
  rfree(m->buckets);
  pthread_mutex_unlock(&m->lock);
  pthread_mutex_destroy(&m->lock);
  rfree(m);
}