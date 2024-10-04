#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "rstring.h"
#include "memory.h"

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