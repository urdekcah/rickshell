#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>
#include <assert.h>
#include "rstring.h"
#include "memory.h"
#include "io.h"

string string__create(const char* s, size_t len) {
  string str;
  str.str = rstrdup(s);
  str.str[len] = '\0';
  str.len = len;
  str.is_lit = 0;
  return str;
}

string string__new(const char* s) {
  return string__create(s, strlen(s));
}

string string__from(string s) {
  return string__create(s.str, s.len);
}

char* string__to_cstr(string s) {
  char* cstr = (char*)rmalloc(s.len + 1);
  memcpy(cstr, s.str, s.len);
  cstr[s.len] = '\0';
  return cstr;
}

size_t string__length(string s) {
  return s.len;
}

string string__upper(string s) {
  string result = string__create(s.str, s.len);
  for (size_t i = 0; i < result.len; i++)
    result.str[i] = (char)toupper(result.str[i]);
  return result;
}

string string__lower(string s) {
  string result = string__create(s.str, s.len);
  for (size_t i = 0; i < result.len; i++)
    result.str[i] = (char)tolower(result.str[i]);
  return result;
}

string string__trim(string s) {
  size_t start = 0, end = s.len;
  while (start < s.len && isspace(s.str[start])) start++;
  while (end > start && isspace(s.str[end - 1])) end--;
  return string__create(s.str + start, end - start);
}

string string__ltrim(string s) {
  size_t start = 0;
  while (start < s.len && isspace(s.str[start])) start++;
  return string__create(s.str + start, s.len - start);
}

string string__rtrim(string s) {
  size_t end = s.len;
  while (end > 0 && isspace(s.str[end - 1])) end--;
  return string__create(s.str, end);
}

string string__substring2(string s, ssize_t start) {
  if (start >= (ssize_t)s.len) return string__create("", 0);
  return string__create(s.str + start, s.len - (size_t)start);
}

string string__substring3(string s, ssize_t start, ssize_t end) {
  if (start >= (ssize_t)s.len) return string__create("", 0);
  if (end > (ssize_t)s.len) end = (ssize_t)s.len;
  if (start > end) start = end;
  return string__create(s.str + start, (size_t)(end - start));
}

string string__replace(string s, string old, string new) {
  ssize_t pos = string__indexof(s, old);
  if (pos == -1) return string__create(s.str, s.len);
    
  size_t result_len = s.len - old.len + new.len;
  char* result = (char*)rmalloc(result_len + 1);

  memcpy(result, s.str, (size_t)pos);
  memcpy(result + pos, new.str, new.len);
  memcpy(result + pos + new.len, s.str + pos + old.len, s.len - (size_t)pos - old.len);
  result[result_len] = '\0';
    
  return (string){.str = result, .len = result_len, .is_lit = 0};
}

string string__replace_all(string s, string old, string new) {
  ssize_t pos = 0;
  string result = string__create(s.str, s.len);
  while ((pos = string__indexof(result, old)) != -1) {
    string temp = string__replace(result, old, new);
    string__free(result);
    result = temp;
  }
  return result;
}

string string__concat(string s1, string s2) {
  size_t new_len = s1.len + s2.len;
  char* new_str = (char*)rmalloc(new_len + 1);
  memcpy(new_str, s1.str, s1.len);
  memcpy(new_str + s1.len, s2.str, s2.len);
  new_str[new_len] = '\0';
  return (string){.str = new_str, .len = new_len, .is_lit = 0};
}

string string__concat_many(int count, ...) {
  va_list args;
  va_start(args, count);
    
  size_t total_len = 0;
  for (int i = 0; i < count; i++) {
    string s = va_arg(args, string);
    total_len += s.len;
  }
    
  char* new_str = (char*)malloc(total_len + 1);
  if (!new_str) {
    ffprintln(stderr, "Memory allocation failed");
    exit(1);
  }
    
  va_start(args, count);
  size_t pos = 0;
  for (int i = 0; i < count; i++) {
    string s = va_arg(args, string);
    memcpy(new_str + pos, s.str, s.len);
    pos += s.len;
  }
  new_str[total_len] = '\0';
    
  va_end(args);
  return (string){.str = new_str, .len = total_len, .is_lit = 0};
}

bool string__contains(string s, string value) {
  return string__indexof(s, value) != -1;
}

bool string__startswith(string s, string prefix) {
  if (prefix.len > s.len) return false;
  return memcmp(s.str, prefix.str, prefix.len) == 0;
}

bool string__endswith(string s, string suffix) {
  if (suffix.len > s.len) return false;
  return memcmp(s.str + s.len - suffix.len, suffix.str, suffix.len) == 0;
}

ssize_t string__indexof(string s, string substr) {
  if (substr.len > s.len) return -1;
  for (size_t i = 0; i <= s.len - substr.len; i++) {
    if (memcmp(s.str + i, substr.str, substr.len) == 0)
      return (ssize_t)i;
  }
  return -1;
}

ssize_t string__lastindexof(string s, string substr) {
  if (substr.len > s.len) return -1;
  for (size_t i = s.len - substr.len; i != (size_t)-1; i--) {
    if (memcmp(s.str + i, substr.str, substr.len) == 0)
      return (ssize_t)i;
  }
  return -1;
}

ssize_t string__indexof_any(string s, string substrs[]) {
  ssize_t min_index = -1;
  for (size_t i = 0; substrs[i].str != NULL; i++) {
    ssize_t index = string__indexof(s, substrs[i]);
    if (index != (ssize_t)-1 && index < min_index)
      min_index = index;
  }
  return min_index;
}

ssize_t string__lastindexof_any(string s, string substrs[]) {
  ssize_t max_index = -1;
  for (ssize_t i = 0; substrs[i].str != NULL; i++) {
    ssize_t index = string__lastindexof(s, substrs[i]);
    if (index != -1 && index > max_index)
      max_index = index;
  }
  return max_index;
}

string string__remove2(string s, size_t start) {
  if (start >= s.len) return string__create(s.str, s.len);
  return string__create(s.str, start);
}

string string__remove3(string s, size_t start, size_t end) {
  if (start >= s.len) return string__create(s.str, s.len);
  if (end > s.len) end = s.len;
  if (start > end) start = end;
  size_t new_len = s.len - (end - start);
  char* new_str = (char*)rmalloc(new_len + 1);
  memcpy(new_str, s.str, start);
  memcpy(new_str + start, s.str + end, s.len - end);
  new_str[new_len] = '\0';
  return (string){.str = new_str, .len = new_len, .is_lit = 0};
}

StringArray string__split(string s, string delim) {
  ssize_t pos = 0;
  ssize_t prev = 0;
  StringArray result = create_array(sizeof(string));
  string tok = string__substring(s, prev);

  while ((pos = string__indexof(tok, delim)) != -1) {
    string part = string__substring(s, prev, prev + pos);
    array_push(&result, &part);
    prev += pos + (ssize_t)delim.len;
    string__free(tok);
    tok = string__substring(s, prev);
  }
  string__free(tok);
  
  if (prev < (ssize_t)s.len) {
    string part = string__substring(s, prev);
    array_push(&result, &part);
  }
    
  return result;
}

string string__join(StringArray strings, string delim) {
  size_t total_len = 0;
  for (size_t i = 0; i < strings.size; i++) {
    string s = *(string*)array_get(strings, i);
    total_len += s.len;
  }
    
  size_t delim_len = delim.len;
  size_t result_len = total_len + (strings.size - 1) * delim_len;
  char* result = (char*)rmalloc(result_len + 1);
    
  size_t pos = 0;
  for (size_t i = 0; i < strings.size; i++) {
    string s = *(string*)array_get(strings, i);
    memcpy(result + pos, s.str, s.len);
    pos += s.len;
    if (i < strings.size - 1) {
      memcpy(result + pos, delim.str, delim.len);
      pos += delim.len;
    }
  }
  result[result_len] = '\0';
    
  return (string){.str = result, .len = result_len, .is_lit = 0};
}

int string__compare(string s1, string s2) {
  size_t min_len = s1.len < s2.len ? s1.len : s2.len;
  int cmp = memcmp(s1.str, s2.str, min_len);
  if (cmp != 0) return cmp;
  if (s1.len < s2.len) return -1;
  if (s1.len > s2.len) return 1;
  return 0;
}

bool string__equals(string s1, string s2) {
  return string__compare(s1, s2) == 0;
}

string string__ljust(string s, size_t width, char pad) {
  if (width <= s.len) return string__create(s.str, s.len);
  char* new_str = (char*)rmalloc(width + 1);
  memcpy(new_str, s.str, s.len);
  memset(new_str + s.len, pad, width - s.len);
  new_str[width] = '\0';
  return (string){.str = new_str, .len = width, .is_lit = 0};
}

string string__rjust(string s, size_t width, char pad) {
  if (width <= s.len) return string__create(s.str, s.len);
  char* new_str = (char*)rmalloc(width + 1);
  memset(new_str, pad, width - s.len);
  memcpy(new_str + width - s.len, s.str, s.len);
  new_str[width] = '\0';
  return (string){.str = new_str, .len = width, .is_lit = 0};
}

bool string__is_null_or_empty(string s) {
  return s.str == NULL || s.len == 0 || (s.len == 1 && s.str[0] == '\0');
}

bool string__is_null_or_whitespace(string s) {
  if (s.str == NULL) return true;
  for (size_t i = 0; i < s.len; i++)
    if (!isspace((unsigned char)s.str[i])) return false;
  return true;
}

string string__zfill(string s, size_t width) {
  if (width <= s.len) return string__create(s.str, s.len);
  char* new_str = (char*)rmalloc(width + 1);
  size_t padding = width - s.len;
  memset(new_str, '0', padding);
  memcpy(new_str + padding, s.str, s.len);
  new_str[width] = '\0';
  return (string){.str = new_str, .len = width, .is_lit = 0};
}

string string__reverse(string s) {
  char* new_str = (char*)rmalloc(s.len + 1);
  for (size_t i = 0; i < s.len; i++)
    new_str[i] = s.str[s.len - 1 - i];
  new_str[s.len] = '\0';
  return (string){.str = new_str, .len = s.len, .is_lit = 0};
}

string string__repeat(string s, size_t count) {
  size_t new_len = s.len * count;
  char* new_str = (char*)rmalloc(new_len + 1);
  for (size_t i = 0; i < count; i++)
    memcpy(new_str + i * s.len, s.str, s.len);
  new_str[new_len] = '\0';
  return (string){.str = new_str, .len = new_len, .is_lit = 0};
}

string string__capitalize(string s) {
  if (s.len == 0) return string__create("", 0);
  char* new_str = (char*)rmalloc(s.len + 1);
  new_str[0] = (char)toupper((unsigned char)s.str[0]);
  for (size_t i = 1; i < s.len; i++)
    new_str[i] = (char)tolower((unsigned char)s.str[i]);
  new_str[s.len] = '\0';
  return (string){.str = new_str, .len = s.len, .is_lit = 0};
}

string string__swapcase(string s) {
  char* new_str = (char*)rmalloc(s.len + 1);
  for (size_t i = 0; i < s.len; i++) {
    if (isupper((unsigned char)s.str[i])) {
      new_str[i] = (char)tolower((unsigned char)s.str[i]);
    } else if (islower((unsigned char)s.str[i])) {
      new_str[i] = (char)toupper((unsigned char)s.str[i]);
    } else {
      new_str[i] = s.str[i];
    }
  }
  new_str[s.len] = '\0';
  return (string){.str = new_str, .len = s.len, .is_lit = 0};
}

string string__remove_prefix(string s, string prefix, bool is_longest_match) {
  if (is_longest_match) {
    if (string__startswith(s, prefix))
      return string__substring(s, (ssize_t)prefix.len);
  } else {
    if (s.len >= prefix.len && memcmp(s.str, prefix.str, prefix.len) == 0)
      return string__substring(s, (ssize_t)prefix.len);
  }
  return string__create(s.str, s.len);
}

string string__remove_suffix(string s, string suffix, bool greedy) {
  if (greedy) {
    if (string__endswith(s, suffix))
      return string__substring(s, 0, (ssize_t)(s.len - suffix.len));
  } else {
    if (s.len >= suffix.len && memcmp(s.str + s.len - suffix.len, suffix.str, suffix.len) == 0)
      return string__substring(s, 0, (ssize_t)(s.len - suffix.len));
  }
  return string__create(s.str, s.len);
}

string string__remove_quotes(string s) {
  if (string__length(s) < 2) return string__from(s);
  if ((s.str[0] == '"' && s.str[s.len - 1] == '"') || (s.str[0] == '\'' && s.str[s.len - 1] == '\''))
    return string__substring(s, 1, (ssize_t)(s.len - 1));
  return string__from(s);
}

bool string__isdigit(string s) {
  if (s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++)
    if (!isdigit((unsigned char)s.str[i])) return false;
  return true;
}

bool string__isdecimal(string s) {
  return string__isdigit(s);
}

bool string__isalpha(string s) {
  if (s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++)
    if (!isalpha((unsigned char)s.str[i])) return false;
  return true;
}

bool string__isalnum(string s) {
  if (s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++)
    if (!isalnum((unsigned char)s.str[i])) return false;
  return true;
}

bool string__isprintable(string s) {
  for (size_t i = 0; i < s.len; i++)
    if (!isprint((unsigned char)s.str[i])) return false;
  return true;
}

bool string__isupper(string s) {
  if (s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++)
    if (!isupper((unsigned char)s.str[i]) && isalpha((unsigned char)s.str[i])) return false;
  return true;
}

bool string__islower(string s) {
  if (s.len == 0) return false;
  for (size_t i = 0; i < s.len; i++)
    if (!islower((unsigned char)s.str[i]) && isalpha((unsigned char)s.str[i])) return false;
  return true;
}

bool string__istitle(string s) {
  if (s.len == 0) return false;
  bool prev_is_cased = false;
  for (size_t i = 0; i < s.len; i++) {
    if (isalpha((unsigned char)s.str[i])) {
      if ((prev_is_cased && isupper((unsigned char)s.str[i])) ||
          (!prev_is_cased && islower((unsigned char)s.str[i]))) {
            return false;
      }
      prev_is_cased = true;
    } else {
      prev_is_cased = false;
    }
  }
  return true;
}

bool string__isidentifier(string s) {
  if (s.len == 0) return false;
  if (!isalpha((unsigned char)s.str[0]) && s.str[0] != '_') return false;
  for (size_t i = 1; i < s.len; i++)
    if (!isalnum((unsigned char)s.str[i]) && s.str[i] != '_') return false;
  return true;
}

char string__min(string s) {
  if (s.len == 0) return '\0';
  char min_char = s.str[0];
  for (size_t i = 1; i < s.len; i++) {
    if (s.str[i] < min_char)
      min_char = s.str[i];
  }
  return min_char;
}

char string__max(string s) {
  if (s.len == 0) return '\0';
  char max_char = s.str[0];
  for (size_t i = 1; i < s.len; i++) {
    if (s.str[i] > max_char) {
      max_char = s.str[i];
    }
  }
  return max_char;
}

void rstring__upper(string s) {
  for (size_t i = 0; i < s.len; i++)
    s.str[i] = (char)toupper(s.str[i]);
}

void rstring__lower(string s) {
  for (size_t i = 0; i < s.len; i++)
    s.str[i] = (char)tolower(s.str[i]);
}

void rstring__trim(string s) {
  size_t start = 0, end = s.len;
  while (start < s.len && isspace(s.str[start])) start++;
  while (end > start && isspace(s.str[end - 1])) end--;
  memmove(s.str, s.str + start, end - start);
  s.len = end - start;
  s.str[s.len] = '\0';
}

void string__free(string s) {
  if (s.is_lit) return;
  if (s.is_lit == -997) {
    println(_SLIT("Double free detected"));
    return;
  }
  s.is_lit = -997;
  rfree(s.str);
}

static void string_builder__resize(StringBuilder* sb, size_t new_capacity) {
  assert(sb != NULL);
  if (new_capacity < 10)
    new_capacity = 10;
  char* new_buffer = rrealloc(sb->buffer, new_capacity);
  sb->buffer = new_buffer;
  sb->capacity = new_capacity;
}

StringBuilder string_builder__new() {
  return string_builder__with_capacity(10);
}

StringBuilder string_builder__with_capacity(size_t capacity) {
  StringBuilder sb;
  if (capacity < 10)
    capacity = 10;
  sb.buffer = rmalloc(capacity);
  sb.len = 0;
  sb.capacity = capacity;
  sb.buffer[0] = '\0';
  return sb;
}

StringBuilder string_builder__from_string(string s) {
  StringBuilder sb = string_builder__with_capacity(s.len + 1);
  string_builder__append(&sb, s);
  return sb;
}

void string_builder__append(StringBuilder* sb, string s) {
  assert(sb != NULL);
  size_t s_len = string__length(s);
  if (sb->len + s_len + 1 > sb->capacity)
    string_builder__resize(sb, (sb->len + s_len + 1) * 2);
  memcpy(sb->buffer + sb->len, s.str, s_len);
  sb->len += s_len;
  sb->buffer[sb->len] = '\0';
}

void string_builder__append_cstr(StringBuilder* sb, const char* s) {
  assert(sb != NULL);
  size_t s_len = strlen(s);
  if (sb->len + s_len + 1 > sb->capacity)
    string_builder__resize(sb, (sb->len + s_len + 1) * 2);
  memcpy(sb->buffer + sb->len, s, s_len);
  sb->len += s_len;
  sb->buffer[sb->len] = '\0';
}

void string_builder__append_char(StringBuilder* sb, char c) {
  assert(sb != NULL);
  if (sb->len + 2 > sb->capacity)
    string_builder__resize(sb, sb->len + 2);
  sb->buffer[sb->len++] = c;
  sb->buffer[sb->len] = '\0';
}

void string_builder__append_short(StringBuilder* sb, short value) {
  char buffer[7];
  int result = snprintf(buffer, sizeof(buffer), "%hd", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_int(StringBuilder* sb, int value) {
  char buffer[12];
  int result = snprintf(buffer, sizeof(buffer), "%d", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_long(StringBuilder* sb, long value) {
  char buffer[21];
  int result = snprintf(buffer, sizeof(buffer), "%ld", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_long_long(StringBuilder* sb, long long value) {
  char buffer[21];
  int result = snprintf(buffer, sizeof(buffer), "%lld", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_float(StringBuilder* sb, float value) {
  char buffer[32];
  int result = snprintf(buffer, sizeof(buffer), "%f", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_double(StringBuilder* sb, double value) {
  char buffer[32];
  int result = snprintf(buffer, sizeof(buffer), "%f", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__append_long_double(StringBuilder* sb, long double value) {
  char buffer[32];
  int result = snprintf(buffer, sizeof(buffer), "%Lf", value);
  string temp = (result < 0) ? _SLIT0 : string__new(buffer);
  string_builder__append(sb, temp);
  string__free(temp);
}

void string_builder__insert(StringBuilder* sb, size_t index, string s) {
  assert(sb != NULL);
  if (index > sb->len)
    index = sb->len;
  size_t s_len = string__length(s);
  if (sb->len + s_len + 1 > sb->capacity)
    string_builder__resize(sb, (sb->len + s_len + 1) * 2);
  memmove(sb->buffer + index + s_len, sb->buffer + index, sb->len - index + 1);
  memcpy(sb->buffer + index, s.str, s_len);
  sb->len += s_len;
}

void string_builder__insert_cstr(StringBuilder* sb, size_t index, const char* s) {
  assert(sb != NULL);
  if (index > sb->len)
    index = sb->len;
  size_t s_len = strlen(s);
  if (sb->len + s_len + 1 > sb->capacity)
    string_builder__resize(sb, (sb->len + s_len + 1) * 2);
  memmove(sb->buffer + index + s_len, sb->buffer + index, sb->len - index + 1);
  memcpy(sb->buffer + index, s, s_len);
  sb->len += s_len;
}

void string_builder__insert_char(StringBuilder* sb, size_t index, char c) {
  assert(sb != NULL);
  if (index > sb->len)
    index = sb->len;
  if (sb->len + 2 > sb->capacity)
    string_builder__resize(sb, sb->len + 2);
  memmove(sb->buffer + index + 1, sb->buffer + index, sb->len - index + 1);
  sb->buffer[index] = c;
  sb->len++;
}

void string_builder__remove3(StringBuilder* sb, size_t start, size_t end) {
  assert(sb != NULL);
  if (start >= sb->len) return;
  if (end > sb->len) end = sb->len;
  if (start >= end) return;
  memmove(sb->buffer + start, sb->buffer + end, sb->len - end + 1);
  sb->len -= (end - start);
}

void string_builder__remove2(StringBuilder* sb, size_t start) {
  string_builder__remove3(sb, start, sb->len);
}

bool string_builder__equals(StringBuilder* sb, StringBuilder* other) {
  assert(sb != NULL && other != NULL);
  return (sb->len == other->len) && (memcmp(sb->buffer, other->buffer, sb->len) == 0);
}

string string_builder__to_string(StringBuilder* sb) {
  assert(sb != NULL);
  return string__create(sb->buffer, sb->len);
}

void string_builder__clear(StringBuilder* sb) {
  assert(sb != NULL);
  sb->len = 0;
  memset(sb->buffer, 0, sb->capacity);
}

void string_builder__free(StringBuilder* sb) {
  assert(sb != NULL);
  rfree(sb->buffer);
  sb->buffer = NULL;
  sb->len = 0;
  sb->capacity = 0;
}