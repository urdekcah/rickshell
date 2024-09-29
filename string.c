#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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