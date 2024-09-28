#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "variable.h"
#include "memory.h"
#include "error.h"

#define INITIAL_TABLE_SIZE 10

VariableTable* variable_table = NULL;

void init_variables() {
  variable_table = create_variable_table();
}

VariableTable* create_variable_table() {
  VariableTable* table = rmalloc(sizeof(VariableTable));
  table->variables = rmalloc(INITIAL_TABLE_SIZE * sizeof(Variable));
  table->size = 0;
  table->capacity = INITIAL_TABLE_SIZE;
  return table;
}

void free_variable_table(VariableTable* table) {
  for (int i = 0; i < table->size; i++) {
    rfree(table->variables[i].name);
    rfree(table->variables[i].value);
    if (table->variables[i].is_array) {
      for (int j = 0; j < table->variables[i].array_size; j++) {
        rfree(table->variables[i].array_values[j]);
      }
      rfree(table->variables[i].array_values);
    }
  }
  rfree(table->variables);
  rfree(table);
}

static void expand_table(VariableTable* table) {
  table->capacity *= 2;
  table->variables = rrealloc(table->variables, table->capacity * sizeof(Variable));
}

Variable* set_variable(VariableTable* table, const char* name, const char* value, bool readonly) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      if (table->variables[i].readonly) {
        print_error("Cannot modify readonly variable");
        return NULL;
      }
      rfree(table->variables[i].value);
      table->variables[i].value = rstrdup(value);
      table->variables[i].readonly = readonly;
      return &table->variables[i];
    }
  }

  if (table->size == table->capacity) {
    expand_table(table);
  }

  Variable* var = &table->variables[table->size++];
  var->name = rstrdup(name);
  var->value = rstrdup(value);
  var->readonly = readonly;
  var->is_array = false;
  var->array_values = NULL;
  var->array_size = 0;

  return var;
}

Variable* get_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      return &table->variables[i];
    }
  }
  return NULL;
}

void unset_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      if (table->variables[i].readonly) {
        print_error("Cannot unset readonly variable");
        return;
      }
      rfree(table->variables[i].name);
      rfree(table->variables[i].value);
      if (table->variables[i].is_array) {
        for (int j = 0; j < table->variables[i].array_size; j++) {
          rfree(table->variables[i].array_values[j]);
        }
        rfree(table->variables[i].array_values);
      }
      memmove(&table->variables[i], &table->variables[i + 1], (table->size - i - 1) * sizeof(Variable));
      table->size--;
      return;
    }
  }
}

static char* substring(const char* str, int start, int length) {
  char* result = rmalloc(length + 1);
  strncpy(result, str + start, length);
  result[length] = '\0';
  return result;
}

static char* str_replace(const char* src, const char* old, const char* new, bool replace_all) {
  char* result;
  int i, cnt = 0;
  int newlen = strlen(new);
  int oldlen = strlen(old);

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

static char* remove_suffix(const char* str, const char* suffix, bool greedy) {
  int suffix_len = strlen(suffix);
  int str_len = strlen(str);
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

char* expand_variables(VariableTable* table, const char* input) {
  size_t result_size = strlen(input) * 2;
  char* result = rmalloc(result_size);
  char* output = result;
  const char* p = input;

  while (*p) {
    if (*p == '$') {
      if (*(p + 1) == '{') {
        const char* end = strchr(p + 2, '}');
        if (end) {
          size_t var_name_len = end - (p + 2);
          char* var_name = rmalloc(var_name_len + 1);
          strncpy(var_name, p + 2, var_name_len);
          var_name[var_name_len] = '\0';

          char* colon = strchr(var_name, ':');
          char* hash = strchr(var_name, '#');
          char* slash = strchr(var_name, '/');
          char* percent = strchr(var_name, '%');
          char* question = strchr(var_name, '?');

          if (colon && (*(colon + 1) == '-' || *(colon + 1) == '=' || *(colon + 1) == '+')) {
            *colon = '\0';
            char* value = colon + 2;
            char operation = *(colon + 1);

            Variable* var = get_variable(table, var_name);
            if (operation == '+') {
              if (var && var->value && strlen(var->value) > 0) {
                strcpy(output, value);
                output += strlen(value);
              }
            } else {
              if (!var || !var->value || strlen(var->value) == 0) {
                if (operation == '=') {
                  var = set_variable(table, var_name, value, false);
                }
                strcpy(output, value);
                output += strlen(value);
              } else {
                strcpy(output, var->value);
                output += strlen(var->value);
              }
            }
          } else if (question) {
            *question = '\0';
            char* error_message = question + 1;

            Variable* var = get_variable(table, var_name);
            if (!var || !var->value || strlen(var->value) == 0) {
              print_error(error_message);
            } else {
              strcpy(output, var->value);
              output += strlen(var->value);
            }
          } else if (slash) {
            *slash = '\0';
            char* pattern = slash + 1;
            bool replace_all = (*pattern == '/');
            if (replace_all) pattern++;
            
            char* replacement = strchr(pattern, '/');
            if (replacement) {
              *replacement = '\0';
              replacement++;
              
              Variable* var = get_variable(table, var_name);
              if (var) {
                char* new_value = str_replace(var->value, pattern, replacement, replace_all);
                size_t new_len = strlen(new_value);
                if (output - result + new_len >= result_size) {
                  size_t current_len = output - result;
                  result_size *= 2;
                  result = rrealloc(result, result_size);
                  output = result + current_len;
                }
                strcpy(output, new_value);
                output += new_len;
                rfree(new_value);
              }
            }
          } else if (colon) {
            *colon = '\0';
            Variable* var = get_variable(table, var_name);
            if (var) {
              int offset = atoi(colon + 1);
              char* length_str = strchr(colon + 1, ':');
              if (length_str) {
                *length_str = '\0';
                length_str++;
                int length = atoi(length_str);
                if (offset >= 0 && length > 0 && (size_t)(offset + length) <= strlen(var->value)) {
                  char* substr = substring(var->value, offset, length);
                  strcpy(output, substr);
                  output += strlen(substr);
                  rfree(substr);
                }
              }
            }
          } else if (hash) {
            *hash = '\0';
            Variable* var = get_variable(table, hash + 1);
            if (var) {
              int length = strlen(var->value);
              char length_str[20];
              snprintf(length_str, sizeof(length_str), "%d", length);
              strcpy(output, length_str);
              output += strlen(length_str);
            }
          } else if (percent) {
            *percent = '\0';
            char* pattern = percent + 1;
            Variable* var = get_variable(table, var_name);
            if (var) {
              bool greedy = (*(percent + 1) == '%');
              if (greedy) pattern++;
              char* new_value = remove_suffix(var->value, pattern, greedy);
              strcpy(output, new_value);
              output += strlen(new_value);
              rfree(new_value);
            }
          } else {
            Variable* var = get_variable(table, var_name);
            if (var) {
              strcpy(output, var->value);
              output += strlen(var->value);
            }
          }
          rfree(var_name);
          p = end + 1;
          continue;
        }
      } else if (*(p + 1) == '?') {
        char exit_status[20];
        snprintf(exit_status, sizeof(exit_status), "%d", WEXITSTATUS(system(NULL)));
        strcpy(output, exit_status);
        output += strlen(exit_status);
        p += 2;
        continue;
      } else if (*(p + 1) == '!') {
        strcpy(output, "LAST_BG_PID");
        output += strlen("LAST_BG_PID");
        p += 2;
        continue;
      } else if (isalpha(*(p + 1)) || *(p + 1) == '_') {
        const char* var_start = p + 1;
        const char* var_end = var_start;
        while (isalnum(*var_end) || *var_end == '_') var_end++;
        
        size_t var_name_len = var_end - var_start;
        char* var_name = rmalloc(var_name_len + 1);
        strncpy(var_name, var_start, var_name_len);
        var_name[var_name_len] = '\0';

        Variable* var = get_variable(table, var_name);
        if (var) {
          strcpy(output, var->value);
          output += strlen(var->value);
        }
        rfree(var_name);
        p = var_end;
        continue;
      }
    }
    *output++ = *p++;
  }
  *output = '\0';

  return result;
}

void export_variable(VariableTable* table, const char* name) {
  Variable* var = get_variable(table, name);
  if (var) {
    setenv(name, var->value, 1);
  } else {
    print_error("Variable not found");
  }
}

void set_array_variable(VariableTable* table, const char* name, char** values, int size) {
  Variable* var = get_variable(table, name);
  if (var) {
    if (var->readonly) {
      print_error("Cannot modify readonly variable");
      return;
    }
    if (var->is_array) {
      for (int i = 0; i < var->array_size; i++) {
        rfree(var->array_values[i]);
      }
      rfree(var->array_values);
    }
  } else {
    var = set_variable(table, name, "", false);
  }

  var->is_array = true;
  var->array_values = rmalloc(size * sizeof(char*));
  var->array_size = size;

  for (int i = 0; i < size; i++) {
    var->array_values[i] = rstrdup(values[i]);
  }
}