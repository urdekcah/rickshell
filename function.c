/*
 * function.c — Process-global registry of shell function definitions.
 *
 * Entries are held in a small grow-on-demand array. The expected number of
 * defined functions is modest, so a linear scan keyed on the name is used rather
 * than a hash table. Each entry owns its name string and a deep copy of the
 * function body, taken at definition time so the body outlives the input line
 * that produced it.
 */
#include <stdbool.h>
#include <stddef.h>
#include "function.h"
#include "ast.h"
#include "rstring.h"
#include "memory.h"

typedef struct {
  string name;  /**< Owned function name. */
  Node*  body;  /**< Owned deep copy of the compound-command body. */
} FunctionEntry;

static FunctionEntry* g_entries = NULL;
static size_t         g_count = 0;
static size_t         g_capacity = 0;

void init_functions(void) {
  g_entries = NULL;
  g_count = 0;
  g_capacity = 0;
}

/* Returns the index of @p name, or -1 when it is not defined. */
static ssize_t find_index(const string name) {
  for (size_t i = 0; i < g_count; i++)
    if (string__equals(g_entries[i].name, name)) return (ssize_t)i;
  return -1;
}

void define_function(const string name, const Node* body) {
  ssize_t idx = find_index(name);
  if (idx >= 0) {
    node_free(g_entries[idx].body);
    g_entries[idx].body = node_clone(body);
    return;
  }
  if (g_count == g_capacity) {
    size_t new_cap = (g_capacity == 0) ? 8 : g_capacity * 2;
    g_entries = rrealloc(g_entries, new_cap * sizeof(FunctionEntry));
    g_capacity = new_cap;
  }
  g_entries[g_count].name = string__from(name);
  g_entries[g_count].body = node_clone(body);
  g_count++;
}

Node* find_function(const string name) {
  ssize_t idx = find_index(name);
  return (idx >= 0) ? g_entries[idx].body : NULL;
}

bool unset_function(const string name) {
  ssize_t idx = find_index(name);
  if (idx < 0) return false;
  string__free(g_entries[idx].name);
  node_free(g_entries[idx].body);
  /* Compact by moving the last entry into the freed slot; order is irrelevant. */
  g_entries[idx] = g_entries[g_count - 1];
  g_count--;
  return true;
}

void cleanup_functions(void) {
  for (size_t i = 0; i < g_count; i++) {
    string__free(g_entries[i].name);
    node_free(g_entries[i].body);
  }
  rfree(g_entries);
  g_entries = NULL;
  g_count = 0;
  g_capacity = 0;
}
