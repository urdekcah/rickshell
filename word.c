/*
 * word.c — Construction and teardown for the pre-expansion word type.
 *
 * The lexer builds words segment by segment as it scans across quote
 * boundaries; the expansion pass consumes them. This file holds only the
 * mechanical growth and freeing of the segment array so the lexer and the
 * expander can stay focused on their own logic.
 */
#include <stdbool.h>
#include <stddef.h>
#include "word.h"
#include "memory.h"
#include "rstring.h"

#define WORD_INITIAL_CAP 4

Word* word_new(void) {
  Word* w = rmalloc(sizeof(Word));
  w->data = NULL;
  w->size = 0;
  w->cap = 0;
  return w;
}

void word_free(Word* w) {
  if (w == NULL) return;
  for (size_t i = 0; i < w->size; i++)
    string__free(w->data[i].text);
  rfree(w->data);
  rfree(w);
}

void word_push(Word* w, SegKind kind, string text, bool quoted) {
  if (w->size == w->cap) {
    size_t new_cap = (w->cap == 0) ? WORD_INITIAL_CAP : w->cap * 2;
    w->data = rrealloc(w->data, new_cap * sizeof(WordSeg));
    w->cap = new_cap;
  }
  w->data[w->size].kind = kind;
  w->data[w->size].text = text;
  w->data[w->size].quoted = quoted;
  w->size++;
}

bool word_is_empty(const Word* w) {
  return w->size == 0;
}
