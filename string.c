#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
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