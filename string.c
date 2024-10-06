#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "rstring.h"
#include "memory.h"

string string__create(const char* s, size_t len) {
  string str;
  str.str = rstrdup(s);
  str.len = len;
  str.is_lit = 0;
  return str;
}

string string__new(const char* s) {
  return string__create(s, strlen(s));
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

string string__substring2(string s, size_t start) {
  if (start >= s.len) return string__create("", 0);
  return string__create(s.str + start, s.len - start);
}

string string__substring3(string s, size_t start, size_t end) {
  if (start >= s.len) return string__create("", 0);
  if (end > s.len) end = s.len;
  if (start > end) start = end;
  return string__create(s.str + start, end - start);
}

string string__replace(string s, string old, string new) {
  size_t pos = string__indexof(s, old);
  if (pos == (size_t)-1) return string__create(s.str, s.len);
    
  size_t result_len = s.len - old.len + new.len;
  char* result = (char*)rmalloc(result_len + 1);

  memcpy(result, s.str, pos);
  memcpy(result + pos, new.str, new.len);
  memcpy(result + pos + new.len, s.str + pos + old.len, s.len - pos - old.len);
  result[result_len] = '\0';
    
  return (string){.str = result, .len = result_len, .is_lit = 0};
}

string string__replace_all(string s, string old, string new) {
  size_t pos = 0;
  string result = string__create(s.str, s.len);
  while ((pos = string__indexof(result, old)) != (size_t)-1) {
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
    fprintf(stderr, "Memory allocation failed\n");
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
  return string__indexof(s, value) != (size_t)-1;
}

bool string__startswith(string s, string prefix) {
  if (prefix.len > s.len) return false;
  return memcmp(s.str, prefix.str, prefix.len) == 0;
}

bool string__endswith(string s, string suffix) {
  if (suffix.len > s.len) return false;
  return memcmp(s.str + s.len - suffix.len, suffix.str, suffix.len) == 0;
}

size_t string__indexof(string s, string substr) {
  if (substr.len > s.len) return (size_t)-1;
  for (size_t i = 0; i <= s.len - substr.len; i++) {
    if (memcmp(s.str + i, substr.str, substr.len) == 0)
      return i;
  }
  return (size_t)-1;
}

size_t string__lastindexof(string s, string substr) {
  if (substr.len > s.len) return (size_t)-1;
  for (size_t i = s.len - substr.len; i != (size_t)-1; i--) {
    if (memcmp(s.str + i, substr.str, substr.len) == 0)
      return i;
  }
  return (size_t)-1;
}

size_t string__indexof_any(string s, string substrs[]) {
  size_t min_index = (size_t)-1;
  for (size_t i = 0; substrs[i].str != NULL; i++) {
    size_t index = string__indexof(s, substrs[i]);
    if (index != (size_t)-1 && index < min_index)
      min_index = index;
  }
  return min_index;
}

size_t string__lastindexof_any(string s, string substrs[]) {
  size_t max_index = (size_t)-1;
  for (size_t i = 0; substrs[i].str != NULL; i++) {
    size_t index = string__lastindexof(s, substrs[i]);
    if (index != (size_t)-1 && index > max_index)
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
  size_t pos = 0;
  size_t prev = 0;
  StringArray result = create_array(sizeof(string));
    
  while ((pos = string__indexof(string__substring(s, prev), delim)) != (size_t)-1) {
    string part = string__substring(s, prev, prev + pos);
    array_push(&result, &part);
    prev += pos + delim.len;
  }
    
  if (prev < s.len) {
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
  return s.str == NULL || s.len == 0;
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
      return string__substring(s, prefix.len);
  } else {
    if (s.len >= prefix.len && memcmp(s.str, prefix.str, prefix.len) == 0)
      return string__substring(s, prefix.len);
  }
  return string__create(s.str, s.len);
}

string string__remove_suffix(string s, string suffix, bool greedy) {
  if (greedy) {
    if (string__endswith(s, suffix))
      return string__substring(s, 0, s.len - suffix.len);
  } else {
    if (s.len >= suffix.len && memcmp(s.str + s.len - suffix.len, suffix.str, suffix.len) == 0)
      return string__substring(s, 0, s.len - suffix.len);
  }
  return string__create(s.str, s.len);
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

void string__free(string s) {
  if (s.is_lit) return;
  if (s.is_lit == -997) {
    printf("Double free detected\n");
    return;
  }
  s.is_lit = -997;
  rfree(s.str);
}

char* remove_quotes(const char* str) {
  if (str == NULL) return NULL;
  
  size_t len = strlen(str);
  if (len >= 2 && ((str[0] == '"' && str[len-1] == '"') || (str[0] == '\'' && str[len-1] == '\''))) {
    char* result = rmalloc(len - 1);
    strncpy(result, str + 1, len - 2);
    result[len - 2] = '\0';
    return result;
  }
    
  return rstrdup(str);
}

char* str_replace(const char* src, const char* old, const char* new, bool replace_all) {
  char* result;
  size_t i, cnt = 0;
  size_t newlen = strlen(new);
  size_t oldlen = strlen(old);

  for (i = 0; src[i] != '\0'; i++) {
    if (strstr(&src[i], old) == &src[i]) {
      cnt++;
      i += oldlen - 1;
      if (!replace_all) break;
    }
  }

  result = rmalloc(i + cnt * (newlen - oldlen) + 1);

  i = 0;
  while (*src) {
    if (strstr(src, old) == src) {
      strcpy(&result[i], new);
      i += newlen;
      src += oldlen;
      if (!replace_all) {
        strcat(result, src);
        return result;
      }
    } else {
      result[i++] = *src++;
    }
  }
  result[i] = '\0';
  return result;
}

bool match_pattern(const char* str, const char* pattern) {
  const char* s = str;
  const char* p = pattern;
  
  while (*s && *p) {
    if (*p == '*') {
      p++;
      if (*p == '\0') return true;
      while (*s) {
        if (match_pattern(s, p)) return true;
        s++;
      }
      return false;
    } else if (*p == '?' || *p == *s) {
      s++;
      p++;
    } else {
      return false;
    }
  }
  
  while (*p == '*') p++;
  return *p == '\0';
}

char* remove_prefix(const char* value, const char* pattern, bool is_longest_match) {
  size_t value_len = strlen(value);
  const char* best_match = NULL;

  if (is_longest_match) {
    for (size_t i = 0; i < value_len; i++) {
      if (match_pattern(value + i, pattern))
        best_match = strchr(value + i, '.');
    }
  } else {
    for (size_t i = 0; i < value_len; i++) {
      if (match_pattern(value + i, pattern)) {
        best_match = strchr(value + i, '.');
        break;
      }
    }
  }

  if (best_match != NULL) {
    return strdup(best_match);
  }

  return strdup(value);
}

char* remove_suffix(const char* str, const char* suffix, bool greedy) {
  size_t suffix_len = strlen(suffix);
  size_t str_len = strlen(str);
  char* result = rstrdup(str);

  if (greedy) {
    while (str_len >= suffix_len && strcmp(result + str_len - suffix_len, suffix) == 0) {
      result[str_len - suffix_len] = '\0';
      str_len -= suffix_len;
    }
  } else if (str_len >= suffix_len && strcmp(result + str_len - suffix_len, suffix) == 0) {
    result[str_len - suffix_len] = '\0';
  }

  return result;
}

char* dynstrcpy(char** dest, size_t* dest_size, size_t* dest_len, const char* src) {
  size_t src_len = strlen(src);
  size_t new_len = *dest_len + src_len;

  if (new_len >= *dest_size) {
    size_t new_size = *dest_size;
    while (new_size <= new_len) {
      new_size *= 2;
    }
    *dest = rrealloc(*dest, new_size);
    *dest_size = new_size;
  }

  strcpy(*dest + *dest_len, src);
  *dest_len = new_len;

  return *dest;
}

void ltrim(char* str) {
  if (str == NULL) return;
  
  while (*str && isspace(*str)) str++;
  memmove(str, str + strspn(str, " \t\r\n"), 1 + strlen(str + strspn(str, " \t\r\n")));
}

void rtrim(char* str) {
  if (str == NULL) return;
  
  size_t len = strlen(str);
  while (len > 0 && isspace(str[len - 1])) len--;
  str[len] = '\0';
}

void trim(char* str) {
  ltrim(str);
  rtrim(str);
}

void strupper(char* str) {
  if (str == NULL) return;
  
  for (char* c = str; *c; c++) {
    *c = (char)toupper(*c);
  }
}

void strlower(char* str) {
  if (str == NULL) return;
  
  for (char* c = str; *c; c++) {
    *c = (char)tolower(*c);
  }
}