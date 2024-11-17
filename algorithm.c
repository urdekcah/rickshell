#include <stdlib.h>
#include <math.h>
#include "memory.h"
#include "rstring.h"
#include "algorithm.h"

TrigramArray create_trigram_array() {
  TrigramArray arr;
  arr.capacity = 16;
  arr.size = 0;
  arr.entries = (TrigramEntry*)rmalloc(sizeof(TrigramEntry) * arr.capacity);
  return arr;
}

void add_trigram(TrigramArray* arr, string trigram) {
  for (size_t i = 0; i < arr->size; i++) {
    if (string__equals(arr->entries[i].trigram, trigram)) {
      arr->entries[i].count++;
      return;
    }
  }
  
  if (arr->size >= arr->capacity) {
    arr->capacity *= 2;
    arr->entries = (TrigramEntry*)realloc(arr->entries, sizeof(TrigramEntry) * arr->capacity);
  }
  
  arr->entries[arr->size].trigram = string__from(trigram);
  arr->entries[arr->size].count = 1;
  arr->size++;
}

TrigramArray extract_trigrams(string text) {
  TrigramArray trigrams = create_trigram_array();
  
  if (text.len < 3)
    return trigrams;
  
  for (size_t i = 0; i <= text.len - 3; i++) {
    string trigram = string__substring3(text, (ssize_t)i, (ssize_t)i + 3);
    add_trigram(&trigrams, trigram);
    string__free(trigram);
  }
  
  return trigrams;
}

double calculate_similarity(TrigramArray* trigrams1, TrigramArray* trigrams2) {
  int intersection = 0;
  int union_size = 0;
  
  for (size_t i = 0; i < trigrams1->size; i++) {
    bool found = false;
    for (size_t j = 0; j < trigrams2->size; j++) {
      if (string__equals(trigrams1->entries[i].trigram, trigrams2->entries[j].trigram)) {
        intersection += (trigrams1->entries[i].count < trigrams2->entries[j].count) 
                ? trigrams1->entries[i].count 
                : trigrams2->entries[j].count;
        found = true;
        break;
      }
    }  
    (void)found;
  }
  
  for (size_t i = 0; i < trigrams1->size; i++)
    union_size += trigrams1->entries[i].count;
  for (size_t i = 0; i < trigrams2->size; i++)
    union_size += trigrams2->entries[i].count;
  union_size -= intersection;
  
  return union_size == 0 ? 0.0 : (double)intersection / union_size;
}

void free_trigram_array(TrigramArray* arr) {
  for (size_t i = 0; i < arr->size; i++)
    string__free(arr->entries[i].trigram);
  free(arr->entries);
  arr->size = 0;
  arr->capacity = 0;
}

// Similarity score ranges from 0.0 to 1.0, with higher values indicating more similarity
double trigram_similarity(string text1, string text2) {
  TrigramArray trigrams1 = extract_trigrams(text1);
  TrigramArray trigrams2 = extract_trigrams(text2);  
  double similarity = calculate_similarity(&trigrams1, &trigrams2);
  free_trigram_array(&trigrams1);
  free_trigram_array(&trigrams2);  
  return similarity;
}