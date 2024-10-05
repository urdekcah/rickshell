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
#include "map.h"
#include "iterator.h"

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
    free_va_value(&table->variables[i].data);
  }
  rfree(table->variables);
  rfree(table);
}

Variable* create_new_variable(VariableTable* table, const char* name, VariableType type) {
  Variable* existing = get_variable(table, name);
  if (existing != NULL) return existing;

  if (table->size == table->capacity) {
    table->capacity *= 2;
    table->variables = rrealloc(table->variables, (size_t)(table->capacity * (int)sizeof(Variable)));
  }

  Variable* var = &table->variables[table->size++];
  var->name = rstrdup(name);
  var->value = NULL;
  var->type = type;
  var->flags = 0;
  var->array_size = 0;

  memset(&var->data, 0, sizeof(va_value_t));
  var->data.type = type;
  
  switch (type) {
    case VAR_STRING:
      var->data._str = NULL;
      break;
    case VAR_INTEGER:
      var->data._number = 0;
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      var->data._map = create_map_with_func(vfree_va_value);
      break;
    case VAR_ARRAY:
      var->data._array = create_array(sizeof(va_value_t));
      break;
    default:
      break;
  }

  return var;
}

void process_string_variable(Variable* var) {
  if (!var->data.type == VAR_STRING) return;
  if (var->data._str == NULL) return;
  if (is_variable_flag_set(&var->flags, VarFlag_Uppercase)) strupper(var->data._str);
  else if (is_variable_flag_set(&var->flags, VarFlag_Lowercase)) strlower(var->data._str);
}

void process_exported_variable(Variable* var) {
  if (is_variable_flag_set(&var->flags, VarFlag_Exported)) {
    char* value = va_value_to_string(&var->data);
    setenv(var->name, value, 1);
    rfree(value);
  }
}

Variable* set_variable(VariableTable* table, const char* name, const char* value, VariableType type, bool readonly) {
  Variable* var = get_variable(table, name);
  if (var == NULL) {
    var = create_new_variable(table, name, type);
  } else {
    rfree(var->value);
    free_va_value(&var->data);
  }

  if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
    print_error("Cannot modify readonly variable");
    return NULL;
  }

  var->value = rstrdup(value);
  var->type = type;
  var->data.type = type;

  switch (type) {
    case VAR_STRING:
      var->data._str = rstrdup(value);
      process_string_variable(var);
      break;
    case VAR_INTEGER:
      StrconvResult result = ratoll(value, &var->data._number);
      if (result.is_err) {
        print_error("Failed to convert string to integer");
        return NULL;
      }
      break;
    case VAR_ARRAY:
      if (var->data._array.data) array_free(&var->data._array);
      parse_and_set_array(variable_table, name, value);
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      if (var->data._map) map_free(var->data._map);
      parse_and_set_associative_array(variable_table, name, value);
      break;
    default:
      break;
  }
  if (readonly) set_variable_flag(&var->flags, VarFlag_ReadOnly);
  process_exported_variable(var);
  return var;
}

Variable* get_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      Variable* var = &table->variables[i];
      if (var->type == VAR_NAMEREF) {
        Variable* resolved = resolve_nameref(var);
        if (resolved == NULL) {
          print_error("Failed to resolve nameref");
          return NULL;
        }
        return resolved;
      }
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
      free_va_value(&table->variables[i].data);
      memmove(&table->variables[i], &table->variables[i + 1], (size_t)((table->size - i - 1) * (int)sizeof(Variable)));
      table->size--;
      return;
    }
  }
}

void parse_and_set_array(VariableTable* table, const char* name, const char* value) {
  char* trimmed_value = rstrdup(value + 1);
  trimmed_value[strlen(trimmed_value) - 1] = '\0';
  Variable* var = create_new_variable(table, name, VAR_ARRAY);

  char* token;
  char* rest = trimmed_value;

  while ((token = strtok_r(rest, " ", &rest)) != NULL) {
    VariableType type = parse_variable_type(token);
    va_value_t _value = string_to_va_value(token, type);
    array_push(&var->data._array, &_value);
  }

  rfree(trimmed_value);
}

void parse_and_set_associative_array(VariableTable* table, const char* name, const char* input) {
  if (input == NULL || table == NULL) return;
  Variable* var = create_new_variable(table, name, VAR_ASSOCIATIVE_ARRAY);

  if (input[0] != '{' || input[strlen(input) - 1] != '}') {
    fprintf(stderr, "Invalid input format\n");
    return;
  }

  char* trimmed_input = strndup(input + 1, strlen(input) - 2);

  char* token = strtok(trimmed_input, " ");
  while (token != NULL) {
    char* equal_sign = strchr(token, '=');
    if (equal_sign != NULL) {
      *equal_sign = '\0';
      if (token[0] != '[') {
        fprintf(stderr, "Invalid key format\n");
        free(trimmed_input);
        return;
      }
      char* key = token + 1;
      char* value = equal_sign + 1;
      char* close_bracket = strchr(key, ']');
      if (close_bracket != NULL)
        *close_bracket = '\0';
      set_associative_array_variable(table, var->name, key, value);
    }
    token = strtok(NULL, " ");
  }
  
  free(trimmed_input);
}

void array_set_element(VariableTable* table, const char* name, size_t index, const char* value) {
  Variable* var = get_variable(table, name);
  if (var == NULL || var->type != VAR_ARRAY) {
    print_error("Variable is not an array");
    return;
  }

  if (index >= var->data._array.size) {
    print_error("Index out of bounds");
    return;
  }

  VariableType type = parse_variable_type(value);
  va_value_t new_value = string_to_va_value(value, type);
  array_index_set(&var->data._array, index, &new_value);
}

bool do_not_expand_this_builtin(const char* name) {
  char* do_not_expand[] = {"readonly", "set", "unset", "declare"};
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
  
  Variable* resolved = var;
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
    if (resolved->type != VAR_NAMEREF) break;
  }
  return resolved;
}

void set_associative_array_variable(VariableTable* table, const char* name, const char* key, const char* value) {
  Variable* var = get_variable(table, name);
  if (var == NULL) var = create_new_variable(table, name, VAR_ASSOCIATIVE_ARRAY);

  if (var->type != VAR_ASSOCIATIVE_ARRAY) {
    print_error("Variable is not an associative array");
    return;
  }

  va_value_t outval;
  size_t outval_size;
  MapResult result = map_get(var->data._map, key, &outval, &outval_size);
  if (!result.is_err) {
    free_va_value(&outval);
  }

  VariableType vt = parse_variable_type(value);
  va_value_t new_value = string_to_va_value(value, vt);
  map_insert(var->data._map, key, &new_value, sizeof(va_value_t));
}

char* va_value_to_string(const va_value_t* value) {
  char* result = NULL;
  switch (value->type) {
    case VAR_STRING:
      result = rstrdup(value->_str);
      break;
    case VAR_INTEGER:
      result = rmalloc(32);
      snprintf(result, 32, "%lld", value->_number);
      break;
    case VAR_ARRAY: {
      size_t total_length = 2;
      for (size_t i = 0; i < value->_array.size; ++i) {
        va_value_t element = *(va_value_t*)array_checked_get(value->_array, i);
        char* element_str = va_value_to_string(&element);
        total_length += strlen(element_str) + 1;
        free(element_str);
      }
      result = rmalloc(total_length);
      strcpy(result, "(");
      for (size_t i = 0; i < value->_array.size; ++i) {
        va_value_t element = *(va_value_t*)array_checked_get(value->_array, i);
        char* element_str = va_value_to_string(&element);
        strcat(result, element_str);
        if (i < value->_array.size - 1)
          strcat(result, " ");
        free(element_str);
      }
      strcat(result, ")");
      break;
    }
    case VAR_ASSOCIATIVE_ARRAY: {
      size_t total_length = 4;
      MapIterator it = map_iterator(value->_map);
      while (map_has_next(&it)) {
        const char* key = map_next(&it);
        va_value_t* element = map_iterator_get_value(&it, NULL);
        char* element_str = va_value_to_string(element);
        total_length += strlen(key) + strlen(element_str) + 4;
        free(element_str);
      }
      result = rmalloc(total_length);
      strcpy(result, "{ ");
      map_iterator_reset(&it);
      while (map_has_next(&it)) {
        const char* key = map_next(&it);
        va_value_t* element = map_iterator_get_value(&it, NULL);
        char* element_str = va_value_to_string(element);
        strcat(result, "[");
        strcat(result, key);
        strcat(result, "]=");
        strcat(result, element_str);
        if (map_has_next(&it))
          strcat(result, " ");
        free(element_str);
      }
      strcat(result, " }");
      break;
    }
    default:
      result = rstrdup("");
      break;
  }
  return result;
}

va_value_t string_to_va_value(const char* str, VariableType type) {
  va_value_t result;
  result.type = type;
  switch (type) {
    case VAR_STRING:
      result._str = remove_quotes(str);
      break;
    case VAR_INTEGER:
      result._number = atoll(str);
      break;
    case VAR_ARRAY: {
      result._array = create_array(sizeof(va_value_t));
      char* token;
      char* rest = rstrdup(str + 1);
      rest[strlen(rest) - 1] = '\0';
      while ((token = strtok_r(rest, " ", &rest))) {
        VariableType _type = parse_variable_type(token);
        va_value_t value = string_to_va_value(token, _type);
        array_push(&result._array, &value);
      }
      rfree(rest);
      break;
    }
    case VAR_ASSOCIATIVE_ARRAY: {
      result._map = create_map_with_func(vfree_va_value);
      char* trimmed_input = strndup(str + 1, strlen(str) - 2);
      char* token = strtok(trimmed_input, " ");
      while (token != NULL) {
        char* equal_sign = strchr(token, '=');
        if (equal_sign != NULL) {
          *equal_sign = '\0';
          if (token[0] != '[') {
            fprintf(stderr, "Invalid key format\n");
            free(trimmed_input);
            result.type = VAR_STRING;
            result._str = rstrdup("");
            break;
          }
          char* key = token + 1;
          char* value = equal_sign + 1;
          char* close_bracket = strchr(key, ']');
          if (close_bracket != NULL)
            *close_bracket = '\0';
          VariableType vt = parse_variable_type(value);
          va_value_t new_value = string_to_va_value(value, vt);
          map_insert(result._map, key, &new_value, sizeof(va_value_t));
        }
        token = strtok(NULL, " ");
      }
      free(trimmed_input);
      break;
    }
    default:
      result.type = VAR_STRING;
      result._str = rstrdup("");
      break;
  }
  return result;
}

void free_va_value(va_value_t* value) {
  if (value == NULL) return;
  switch (value->type) {
    case VAR_STRING:
      rfree(value->_str);
      break;
    case VAR_ARRAY:
      for (size_t i = 0; i < value->_array.size; i++) {
        va_value_t* elem = array_checked_get(value->_array, i);
        free_va_value(elem);
      }
      array_free(&value->_array);
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      if (value->_map != NULL)
        map_free(value->_map);
      break;
    case VAR_NAMEREF:
      rfree(value->_str);
    default:
      break;
  }
}

void vfree_va_value(void* value) {
  free_va_value((va_value_t*)value);
  rfree(value);
}

void free_variable(Variable* var) {
  if (var == NULL) return;
  rfree(var->name);
  rfree(var->value);
  free_va_value(&var->data);
  rfree(var);
}

void cleanup_variables() {
  free_variable_table(variable_table);
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
          size_t var_name_len = (size_t)(end - (p + 2));
          char* var_name = rmalloc(var_name_len + 1);
          strncpy(var_name, p + 2, var_name_len);
          var_name[var_name_len] = '\0';

          char* open_bracket = strchr(var_name, '[');
          char* close_bracket = strrchr(var_name, ']');
          char* colon = strchr(var_name, ':');
          char* hash = strchr(var_name, '#');
          char* slash = strchr(var_name, '/');
          char* percent = strchr(var_name, '%');
          char* question = strchr(var_name, '?');
          char* exclamation = strchr(var_name, '!');
          char* caret = strchr(var_name, '^');
          char* comma = strchr(var_name, ',');
          char* at = strchr(var_name, '@');

          if (open_bracket && close_bracket && open_bracket < close_bracket) {
            *open_bracket = '\0';
            *close_bracket = '\0';
            char* key = open_bracket + 1;

            Variable* var = get_variable(table, var_name);
            if (var && var->type == VAR_ASSOCIATIVE_ARRAY) {
              va_value_t value;
              size_t value_size;
              MapResult map_result = map_get(var->data._map, key, &value, &value_size);
              if (!map_result.is_err) {
                char* str_value = va_value_to_string(&value);
                dynstrcpy(&result, &result_size, &result_len, str_value);
                rfree(str_value);
              }
            } else if (var && var->type == VAR_ARRAY) {
              long long index;
              StrconvResult sres = ratoll(key, &index);
              if (sres.is_err) {
                print_error("Invalid key for associative array");
                return NULL;
              }
              if (index >= 0 && index < (long long)var->data._array.size) {
                va_value_t* value = array_checked_get(var->data._array, (size_t)index);
                char* str_value = va_value_to_string(value);
                dynstrcpy(&result, &result_size, &result_len, str_value);
                rfree(str_value);
              }
            }
          } else if (hash) {
            if (*var_name == '#') {
              const char* vname = hash + 1;
              Variable* var = get_variable(table, vname);
              if (var) {
                size_t length = strlen(var->value);
                char length_str[20];
                snprintf(length_str, sizeof(length_str), "%ld", length);
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
            size_t indirect_var_name_len = strlen(indirect_var_name);
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
                    *c = (char)toupper(*c);
                  }
                }
              } else {
                for (char* c = value; *c; c++) {
                  if (strchr(pattern, *c) != NULL) {
                    if (convert_all || c == value) {
                      *c = (char)toupper(*c);
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
                  *c = (char)tolower(*c);
                }
              } else {
                *value = (char)tolower(*value);
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

              size_t var_len = strlen(var->value);
              
              if (offset < 0) {
                offset = (long)var_len + offset;
              }

              if (length_str) {
                long length = strtol(length_str, NULL, 10);
                if (offset >= 0 && length > 0 && offset + length <= (long)var_len) {
                  char* temp = rmalloc((size_t)length + 1);
                  strncpy(temp, var->value + offset, (size_t)length);
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
        
        const char* var_nv = var_end;
        size_t var_name_len = (size_t)(var_end - var_start);
        char* var_name = rmalloc(var_name_len + 1);
        strncpy(var_name, var_start, var_name_len);
        var_name[var_name_len] = '\0';

        Variable* var = get_variable(table, var_name);
        if (var) {
          if (var->type == VAR_ARRAY || var->type == VAR_ASSOCIATIVE_ARRAY) {
            const char* array_index_start = strchr(var_nv, '[');
            if (array_index_start) {
              const char* array_index_end = strchr(array_index_start, ']');
              if (array_index_end) {
                size_t index_len = (size_t)(array_index_end - array_index_start - 1);
                char* temp = rmalloc(index_len + 1);
                strncpy(temp, array_index_start + 1, index_len);
                temp[index_len] = '\0';
                char* index_str = expand_variables(table, temp);
                rfree(temp);

                switch (var->type) {
                  case VAR_ARRAY: {
                    long long index;
                    StrconvResult sres = ratoll(index_str, &index);
                    if (!sres.is_err && index >= 0 && index < (long long)var->data._array.size) {
                      va_value_t* value = array_checked_get(var->data._array, (size_t)index);
                      char* str_value = va_value_to_string(value);
                      dynstrcpy(&result, &result_size, &result_len, str_value);
                      rfree(str_value);
                    }
                    break;
                  }
                  case VAR_ASSOCIATIVE_ARRAY: {
                    va_value_t value;
                    size_t value_size;
                    MapResult map_result = map_get(var->data._map, index_str, &value, &value_size);
                    if (!map_result.is_err) {
                      char* str_value = va_value_to_string(&value);
                      dynstrcpy(&result, &result_size, &result_len, str_value);
                      rfree(str_value);
                    }
                    break;
                  }
                  default:
                    break;
                }
                rfree(index_str);
              }
              p = array_index_end + 1;
            } else {
              char* value = va_value_to_string(&var->data);
              dynstrcpy(&result, &result_size, &result_len, value);
              rfree(value);
            }
          } else {
            char* value = va_value_to_string(&var->data);
            dynstrcpy(&result, &result_size, &result_len, value);
            rfree(value);
          }
        }
        rfree(var_name);
        p = (p > var_end) ? p : var_end;
        continue;
      }
    }
    char temp[2] = {*p, '\0'};
    dynstrcpy(&result, &result_size, &result_len, temp);
    p++;
  }

  return result;
}

bool is_variable_flag_set(va_flag_t* vf, va_flag_t flag) { return (*vf & flag) != 0; }
void set_variable_flag(va_flag_t* vf, va_flag_t flag) { *vf |= flag; }
void unset_variable_flag(va_flag_t* vf, va_flag_t flag) { *vf &= ~flag; }