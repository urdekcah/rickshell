#ifndef __RICKSHELL_ALGORITHM_H__
#define __RICKSHELL_ALGORITHM_H__
#include <stdlib.h>
#include <math.h>
#include "rstring.h"

typedef struct {
  string trigram;
  int count;
} TrigramEntry;

typedef struct {
  TrigramEntry* entries;
  size_t size;
  size_t capacity;
} TrigramArray;

TrigramArray create_trigram_array();
void add_trigram(TrigramArray* arr, string trigram);
TrigramArray extract_trigrams(string text);
double calculate_similarity(TrigramArray* trigrams1, TrigramArray* trigrams2);
void free_trigram_array(TrigramArray* arr);
double trigram_similarity(string text1, string text2);
#endif /* __RICKSHELL_ALGORITHM_H__ */