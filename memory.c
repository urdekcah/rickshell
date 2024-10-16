#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include "io.h"
#include "memory.h"
#include "error.h"

int64_t total_memory_allocated = 0;
int64_t total_memory_freed = 0;

void* rmalloc(size_t size) {
  #if defined(_HYUNSEO_DEV_TRACE_MALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    total_memory_allocated += size;
    ffprintln(stderr, "rmalloc %6d total %10d", size, total_memory_allocated);
  }
  #endif
  if (size == 0) return ((void*)(0));
  void* ptr = malloc(size);
  if (ptr == NULL) {
    ffprintln(stderr, "Error: malloc failed");
    exit(1);
  }
  #if defined(_HYUNSEO_DEV_FEATURE_DEBUG_MALLOC) || defined(_HYUNSEO_DEV_DEBUG_MEMORY)
  {
    memset(ptr, 0x4D, size);
  }
  #endif
  return ptr;
}

void* unsafe_rmalloc(size_t size) {
  #if defined(_HYUNSEO_DEV_TRACE_MALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    total_memory_allocated += size;
    ffprintln(stderr, "rmalloc %6d total %10d", size, total_memory_allocated);
  }
  #endif
  void* ptr = malloc(size);
  if (ptr == NULL) {
    ffprintln(stderr, "Error: unsafe_malloc failed");
    exit(1);
  }
  #if defined(_HYUNSEO_DEV_FEATURE_DEBUG_MALLOC) || defined(_HYUNSEO_DEV_DEBUG_MEMORY)
  {
    memset(ptr, 0x4D, size);
  }
  #endif
  return ptr;
}

void* rcalloc(size_t num, size_t size) {
  #if defined(_HYUNSEO_DEV_TRACE_CALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    total_memory_allocated += num * size;
    ffprintln(stderr, "rcalloc  %6d total %10d", num * size, total_memory_allocated);
  }
  #endif
  if (num == 0 || size == 0) return ((void*)(0));
  void* ptr = calloc(num, size);
  if (ptr == NULL) {
    ffprintln(stderr, "Error: calloc failed");
    exit(1);
  }
  return ptr;
}

void* unsafe_rcalloc(size_t num, size_t size) {
  #if defined(_HYUNSEO_DEV_TRACE_CALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    total_memory_allocated += num * size;
    ffprintln(stderr, "rcalloc  %6d total %10d", num * size, total_memory_allocated);
  }
  #endif
  void* ptr = calloc(num, size);
  return ptr;
}

void* rrealloc(void* ptr, size_t size) {
  #if defined(_HYUNSEO_DEV_TRACE_REALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    ffprintln(stderr, "rrealloc %6d", size);
  }
  #endif
  void* new_ptr = ((void*)(0));
  #if defined(_HYUNSEO_PREALLOC)
  {
    new_ptr = rmalloc(size);
    memcpy(new_ptr, ptr, size);
    return new_ptr;
  }
  #else
  {
    new_ptr = realloc(ptr, size);
  }
  #endif
  if (new_ptr == 0) {
    ffprintln(stderr, "Error: realloc failed");
    exit(1);
  }
  return new_ptr;
}

void* rrealloc_data(void* old_data, size_t old_size, size_t new_size) {
  #if defined(_HYUNSEO_DEV_TRACE_REALLOC) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    ffprintln(stderr, "rrealloc_data old_size: %6d new_size: %6d", old_size, new_size);
  }
  #endif
  #if defined(_HYUNSEO_PREALLOC) 
  {
    return rrealloc(old_data, new_size);
  }
  #endif
  #if defined(_WEIRD_DEFINE_LIKE_HYUNSEO_HOODIES_DEBUG_REALLOC)
  {
    new_ptr = rmalloc(new_size)
    int min_size = (old_size < new_size ? (old_size) : (new_size));
    memcpy(new_ptr, old_data, min_size);
    memset(old_data, 0x57, old_size);
    rfree(old_data);
    return new_ptr;
  }
  #endif
  (void)old_size;
  void* nptr = ((void*)(0));
  nptr = realloc(old_data, new_size);
  if (nptr == 0) {
    ffprintln(stderr, "Error: realloc_data failed");
    exit(1);
  }
  return nptr;
}

void rfree(void* ptr) {
  #if defined(_HYUNSEO_DEV_TRACE_FREE) || defined(_HYUNSEO_DEV_TRACE_MEMORY)
  {
    ffprintln(stderr, "rfree size: %10d", sizeof(ptr));
  }
  #endif
  #if defined(_HYUNSEO_PREALLOC)
  {
    return;
  }
  #else
    free(ptr);
  #endif
}

char *rstrdup(const char *src) {
  size_t len = strlen(src) + 1;
  char* dest = rmalloc(len);
  if (dest == NULL) {
    print_error(_SLIT("Memory allocation failed"));
    return NULL;
  }
  return memcpy(dest, src, len);
}

char* rstrcat(char* dest, const char* src, size_t dest_size) {
  size_t dest_len = strnlen(dest, dest_size);
  size_t src_len = strnlen(src, dest_size - dest_len);
    
  if (dest_len + src_len >= dest_size) {
    print_error(_SLIT("String concatenation would result in buffer overflow"));
    return NULL;
  }
    
  strncat(dest + dest_len, src, src_len);
  dest[dest_size - 1] = '\0';
  return dest;
}