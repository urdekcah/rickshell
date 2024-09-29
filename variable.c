#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "variable.h"
#include "memory.h"
#include "error.h"
#include "expr.h"
#include "execute.h"
#include "strconv.h"
#include "rstring.h"

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
    if (table->variables[i].type == VAR_ARRAY || table->variables[i].type == VAR_ASSOCIATIVE_ARRAY) {
      for (int j = 0; j < table->variables[i].array_size; j++) {
        rfree(table->variables[i].data._array[j]);
      }
      rfree(table->variables[i].data._array);
    }
  }
  rfree(table->variables);
  rfree(table);
}

static void expand_table(VariableTable* table) {
  table->capacity *= 2;
  table->variables = rrealloc(table->variables, table->capacity * sizeof(Variable));
}

Variable* set_variable(VariableTable* table, const char* name, const char* value, VariableType type, bool readonly) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      Variable* var = resolve_nameref(&table->variables[i]);
      if (var == NULL) {
        print_error("Failed to resolve nameref");
        return NULL;
      }

      if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
        print_error("Cannot modify readonly variable");
        return NULL;
      }
      rfree(var->value);
      var->value = rstrdup(value);
      var->type = type;
      if (readonly) set_variable_flag(&var->flags, VarFlag_ReadOnly);

      switch (type) {
      case VAR_STRING:
        var->data._str = remove_quotes(value);
        break;
      case VAR_INTEGER:
        StrconvResult result = ratoll(value, &var->data._number);
        if (result.is_err) {
          print_error("Failed to convert string to integer");
          return NULL;
        }
        break;
      default:
        break;
      }
      
      return var;
    }
  }

  if (table->size == table->capacity) {
    expand_table(table);
  }

  Variable* var = &table->variables[table->size++];
  var->name = rstrdup(name);
  var->value = rstrdup(value);
  var->type = type;
  var->flags = 0;
  var->array_size = 0;
  
  if (type == VAR_INTEGER) {
    var->data._number = atoi(value);
  } else if (type == VAR_ARRAY || type == VAR_ASSOCIATIVE_ARRAY) {
    var->data._array = NULL;
  }

  return var;
}

Variable* get_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      Variable* var = &table->variables[i];
      if (var->type == VAR_NAMEREF) 
        return resolve_nameref(var);
      return var;
    }
  }
  return NULL;
}

void unset_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      if (is_variable_flag_set(&table->variables[i].flags, VarFlag_ReadOnly)) {
        print_error("Cannot unset readonly variable");
        return;
      }
      rfree(table->variables[i].name);
      rfree(table->variables[i].value);
      if (table->variables[i].type == VAR_ARRAY || table->variables[i].type == VAR_ASSOCIATIVE_ARRAY) {
        for (int j = 0; j < table->variables[i].array_size; j++) {
          rfree(table->variables[i].data._array[j]);
        }
        rfree(table->variables[i].data._array);
      }
      memmove(&table->variables[i], &table->variables[i + 1], (table->size - i - 1) * sizeof(Variable));
      table->size--;
      return;
    }
  }
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
  int value_len = strlen(value);
  const char* best_match = NULL;

  if (is_longest_match) {
    for (int i = 0; i < value_len; i++) {
      if (match_pattern(value + i, pattern))
        best_match = strchr(value + i, '.');
    }
  } else {
    for (int i = 0; i < value_len; i++) {
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

char* expand_variables(VariableTable* table, const char* input) {
  size_t result_size = 10;
  size_t result_len = 0;
  char* result = rmalloc(result_size);
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
          char* exclamation = strchr(var_name, '!');
          char* caret = strchr(var_name, '^');
          char* comma = strchr(var_name, ',');
          char* at = strchr(var_name, '@');

          if (hash) {
            if (*var_name == '#') {
              const char* var_name = hash + 1;
              Variable* var = get_variable(table, var_name);
              if (var) {
                int length = strlen(var->value);
                char length_str[20];
                snprintf(length_str, sizeof(length_str), "%d", length);
                dynstrcpy(&result, &result_size, &result_len, length_str);
              }
            } else {
              char* position = strchr(var_name, '#');
              char* pattern_start = rstrdup(position+1);
              var_name[position - var_name] = '\0';
              
              if (pattern_start) {
                bool is_longest_match = (*(pattern_start) == '#');
                const char* pattern = pattern_start + (is_longest_match ? 1 : 0);
                
                Variable* var = get_variable(table, var_name);
                if (var) {
                  char* value = rstrdup(var->value);
                  char* new_value = remove_prefix(value, pattern, is_longest_match);

                  dynstrcpy(&result, &result_size, &result_len, new_value);

                  rfree(value);
                  rfree(new_value);
                }
              }
            }
          } else if (exclamation) {
            char* indirect_var_name = var_name + 1;
            int indirect_var_name_len = strlen(indirect_var_name);
            if (indirect_var_name[indirect_var_name_len - 1] == '*' || indirect_var_name[indirect_var_name_len - 1] == '@') {
              indirect_var_name[indirect_var_name_len - 1] = '\0';
              for (int i = 0; i < table->size; i++) {
                if (strncmp(table->variables[i].name, indirect_var_name, strlen(indirect_var_name)) == 0) {
                  dynstrcpy(&result, &result_size, &result_len, table->variables[i].name);
                  dynstrcpy(&result, &result_size, &result_len, " ");
                }
              }
              if (result_len > 0 && result[result_len - 1] == ' ')
                result_len--;
            } else {
              Variable* indirect_var = get_variable(table, indirect_var_name);
              if (indirect_var) {
                Variable* target_var = get_variable(table, indirect_var->value);
                if (target_var) {
                  dynstrcpy(&result, &result_size, &result_len, target_var->value);
                }
              }
            }
          } else if (caret) {
            *caret = '\0';
            Variable* var = get_variable(table, var_name);
            if (var) {
              char* value = rstrdup(var->value);
              char* pattern = (*(caret + 1) == '^') ? caret + 2 : caret + 1;
              bool convert_all = (*(caret + 1) == '^');
              
              if (*pattern == '\0') {
                for (char* c = value; *c; c++) {
                  if (convert_all || c == value) {
                    *c = toupper(*c);
                  }
                }
              } else {
                for (char* c = value; *c; c++) {
                  if (strchr(pattern, *c) != NULL) {
                    if (convert_all || c == value) {
                      *c = toupper(*c);
                    }
                  }
                }
              }
              dynstrcpy(&result, &result_size, &result_len, value);
              rfree(value);
            }
          } else if (comma) {
            *comma = '\0';
            Variable* var = get_variable(table, var_name);
            if (var) {
              char* value = rstrdup(var->value);
              if (*(comma + 1) == ',') {
                for (char* c = value; *c; c++) {
                  *c = tolower(*c);
                }
              } else {
                *value = tolower(*value);
              }
              dynstrcpy(&result, &result_size, &result_len, value);
              rfree(value);
            }
          } else if (colon) {
            *colon = '\0';
            Variable* var = get_variable(table, var_name);
            if (var) {
              char* endptr;
              long offset = strtol(colon + 1, &endptr, 10);
              char* length_str = (*endptr == ':') ? endptr + 1 : NULL;

              int var_len = strlen(var->value);
              
              if (offset < 0) {
                offset = var_len + offset;
              }

              if (length_str) {
                long length = strtol(length_str, NULL, 10);
                if (offset >= 0 && length > 0 && offset + length <= (long)var_len) {
                  char* temp = rmalloc(length + 1);
                  strncpy(temp, var->value + offset, length);
                  temp[length] = '\0';
                  dynstrcpy(&result, &result_size, &result_len, temp);
                  rfree(temp);
                }
              } else {
                if (offset >= 0 && offset < (long)var_len) {
                  dynstrcpy(&result, &result_size, &result_len, var->value + offset);
                }
              }
            }
          } else if (at) {
            *at = '\0';
            Variable* var = get_variable(table, var_name);
            if (var) {
              if (*(at + 1) == 'Q') {
                dynstrcpy(&result, &result_size, &result_len, "'");
                for (char* c = var->value; *c; c++) {
                  if (*c == '\'') {
                    dynstrcpy(&result, &result_size, &result_len, "'\\''");
                  } else {
                    char temp[2] = {*c, '\0'};
                    dynstrcpy(&result, &result_size, &result_len, temp);
                  }
                }
                dynstrcpy(&result, &result_size, &result_len, "'");
              } else if (*(at + 1) == 'E') {
                int temp = parse_and_execute(var->value);
                char exit_status[20];
                snprintf(exit_status, sizeof(exit_status), "%d", temp);
                dynstrcpy(&result, &result_size, &result_len, exit_status);
              }
            }
          } else if (colon && (*(colon + 1) == '-' || *(colon + 1) == '=' || *(colon + 1) == '+')) {
            *colon = '\0';
            char* value = colon + 2;
            char operation = *(colon + 1);

            Variable* var = get_variable(table, var_name);
            if (operation == '+') {
              if (var && var->value && strlen(var->value) > 0) {
                dynstrcpy(&result, &result_size, &result_len, value);
              }
            } else {
              if (!var || !var->value || strlen(var->value) == 0) {
                if (operation == '=') {
                  var = set_variable(table, var_name, value, parse_variable_type(value), false);
                }
                dynstrcpy(&result, &result_size, &result_len, value);
              } else {
                dynstrcpy(&result, &result_size, &result_len, var->value);
              }
            }
          } else if (question) {
            *question = '\0';
            char* error_message = question + 1;

            Variable* var = get_variable(table, var_name);
            if (!var || !var->value || strlen(var->value) == 0) {
              print_error(error_message);
            } else {
              dynstrcpy(&result, &result_size, &result_len, var->value);
            }
          } else if (slash) {
            *slash = '\0';
            char* pattern = slash + 1;
            bool replace_all = (*pattern == '/');
            if (replace_all) pattern++;
            
            char* replacement = strchr(pattern, '/');
            Variable* var = get_variable(table, var_name);
            if (replacement) {
              *replacement = '\0';
              replacement++;
              
              if (var) {
                char* new_value = str_replace(var->value, pattern, replacement, replace_all);
                dynstrcpy(&result, &result_size, &result_len, new_value);
                rfree(new_value);
              }
            } else {
              if (var) {
                char* temp_result = rstrdup(var->value);
                char* pos = temp_result;
                while ((pos = strstr(pos, pattern)) != NULL) {
                  memmove(pos, pos + strlen(pattern), strlen(pos + strlen(pattern)) + 1);
                  if (!replace_all) break;
                }
                
                dynstrcpy(&result, &result_size, &result_len, temp_result);
                rfree(result);
              }
            }
          } else if (percent) {
            *percent = '\0';
            char* pattern = percent + 1;
            Variable* var = get_variable(table, var_name);
            if (var) {
              bool greedy = (*(percent + 1) == '%');
              if (greedy) pattern++;
              char* new_value = remove_suffix(var->value, pattern, greedy);
              dynstrcpy(&result, &result_size, &result_len, new_value);
              rfree(new_value);
            }
          } else {
            Variable* var = get_variable(table, var_name);
            if (var) {
              dynstrcpy(&result, &result_size, &result_len, var->value);
            }
          }
          rfree(var_name);
          p = end + 1;
          continue;
        }
      } else if (*(p + 1) == '?') {
        char exit_status[20];
        snprintf(exit_status, sizeof(exit_status), "%d", WEXITSTATUS(system(NULL)));
        dynstrcpy(&result, &result_size, &result_len, exit_status);
        p += 2;
        continue;
      } else if (*(p + 1) == '!') {
        dynstrcpy(&result, &result_size, &result_len, "LAST_BG_PID");
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
          dynstrcpy(&result, &result_size, &result_len, var->value);
        }
        rfree(var_name);
        p = var_end;
        continue;
      }
    }
    char temp[2] = {*p, '\0'};
    dynstrcpy(&result, &result_size, &result_len, temp);
    p++;
  }

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
    if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
      print_error("Cannot modify readonly variable");
      return;
    }
    if (var->type == VAR_ARRAY || var->type == VAR_ASSOCIATIVE_ARRAY) {
      for (int i = 0; i < var->array_size; i++) {
        rfree(var->data._array[i]);
      }
      rfree(var->data._array);
    }
  } else {
    var = set_variable(table, name, "", VAR_STRING, false);
  }

  var->type = VAR_ARRAY;
  var->data._array = rmalloc(size * sizeof(char*));
  var->array_size = size;

  for (int i = 0; i < size; i++) {
    var->data._array[i] = rstrdup(values[i]);
  }
}

bool do_not_expand_this_builtin(const char* name) {
  char* do_not_expand[] = {"readonly", "set"};
  for (int i = 0; (unsigned long)i < sizeof(do_not_expand) / sizeof(char*); i++) {
    if (strcmp(name, do_not_expand[i]) == 0) {
      return true;
    }
  }
  return false;
}

VariableType parse_variable_type(const char* value) {
  if (value == NULL || *value == '\0')
    return VAR_STRING;
  while (isspace(*value)) value++;
  const char* end = value + strlen(value) - 1;
  while (end > value && isspace(*end)) end--;
  end++;
  if ((value[0] == '"' && end[-1] == '"') || (value[0] == '\'' && end[-1] == '\''))
    return VAR_STRING;
  char* endptr;
  strtol(value, &endptr, 10);
  if (*endptr == '\0')
    return VAR_INTEGER;
  if (value[0] == '(' && end[-1] == ')')
    return VAR_ARRAY;
  if (value[0] == '{' && end[-1] == '}')
    return VAR_ASSOCIATIVE_ARRAY;
  return VAR_STRING;
}

Variable* resolve_nameref(Variable* var) {
  if (var == NULL || var->type != VAR_NAMEREF) {
    return var;
  }
  
  Variable* resolved = NULL;
  int depth = 0;
  while (resolved != NULL && resolved->type == VAR_NAMEREF) {
    if (depth++ > 100) {
      print_error("Too many levels of indirection");
      return NULL;
    }
    resolved = get_variable(variable_table, resolved->data._str);
    if (resolved == NULL) {
      print_error("Variable not found");
      return NULL;
    }
  }
  return resolved;
}

bool is_variable_flag_set(va_flag_t* vf, va_flag_t flag) {
  return (*vf & flag) != 0;
}

void set_variable_flag(va_flag_t* vf, va_flag_t flag) {
  *vf |= flag;
}

void unset_variable_flag(va_flag_t* vf, va_flag_t flag) {
  *vf &= ~flag;
}