/*
 * param.c — Storage for the shell's positional parameters.
 *
 * The parameters live in one ordered list of owned strings. A snapshot returns
 * an independent deep copy so a function call can save the caller's parameters,
 * install its own, and restore them on return without aliasing.
 */
#include <stddef.h>
#include <stdbool.h>
#include "param.h"
#include "array.h"
#include "rstring.h"

static StringArray g_params;
static bool        g_ready = false;

static void free_param_list(StringArray* a) {
  for (size_t i = 0; i < a->size; i++)
    string__free(*(string*)array_get(*a, i));
  array_free(a);
}

void params_init(void) {
  g_params = create_array(sizeof(string));
  g_ready = true;
}

void params_cleanup(void) {
  if (!g_ready) return;
  free_param_list(&g_params);
  g_ready = false;
}

size_t params_count(void) {
  return g_ready ? g_params.size : 0;
}

string params_get(size_t index) {
  if (!g_ready || index == 0 || index > g_params.size)
    return string__new("");
  return string__from(*(string*)array_get(g_params, index - 1));
}

string params_join(char sep) {
  StringBuilder sb = string_builder__new();
  if (g_ready) {
    for (size_t i = 0; i < g_params.size; i++) {
      if (i > 0) string_builder__append_char(&sb, sep);
      string_builder__append(&sb, *(string*)array_get(g_params, i));
    }
  }
  string r = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return r;
}

StringArray params_snapshot(void) {
  StringArray copy = create_array(sizeof(string));
  if (g_ready) {
    for (size_t i = 0; i < g_params.size; i++) {
      string s = string__from(*(string*)array_get(g_params, i));
      array_push(&copy, &s);
    }
  }
  return copy;
}

void params_replace(StringArray params) {
  if (g_ready) free_param_list(&g_params);
  g_params = params;
  g_ready = true;
}
