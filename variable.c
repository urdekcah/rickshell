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
    string__free(table->variables[i].name);
    string__free(table->variables[i].str);
    free_va_value(&table->variables[i].value);
  }
  rfree(table->variables);
  rfree(table);
}

Variable* create_new_variable(VariableTable* table, const string name, VariableType type) {
  Variable* existing = get_variable(table, name);
  if (existing != NULL) return existing;

  if (table->size == table->capacity) {
    table->capacity *= 2;
    table->variables = rrealloc(table->variables, (size_t)(table->capacity * (int)sizeof(Variable)));
  }

  Variable* var = &table->variables[table->size++];
  var->name = string__from(name);
  var->str = _SLIT0;
  var->flags = 0;
  var->array_size = 0;

  memset(&var->value, 0, sizeof(va_value_t));
  var->value.type = type;
  
  switch (type) {
    case VAR_STRING:
      var->value._str = _SLIT0;
      break;
    case VAR_INTEGER:
      var->value._number = 0;
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      var->value._map = create_map_with_func(vfree_va_value);
      break;
    case VAR_ARRAY:
      var->value._array = create_array(sizeof(va_value_t));
      break;
    default:
      break;
  }

  return var;
}

void process_string_variable(Variable* var) {
  if (!var->value.type == VAR_STRING) return;
  if (string__is_null_or_empty(var->value._str)) return;
  if (is_variable_flag_set(&var->flags, VarFlag_Uppercase)) rstring__upper(var->value._str);
  else if (is_variable_flag_set(&var->flags, VarFlag_Lowercase)) rstring__upper(var->value._str);
}

void process_exported_variable(Variable* var) {
  if (is_variable_flag_set(&var->flags, VarFlag_Exported)) {
    string value = va_value_to_string(&var->value);
    setenv(var->name.str, value.str, 1);
    string__free(value);
  }
}

Variable* set_variable(VariableTable* table, const string name, const string value, VariableType type, bool readonly) {
  Variable* var = get_variable(table, name);
  if (var == NULL) {
    var = create_new_variable(table, name, type);
  } else {
    string__free(var->str);
    free_va_value(&var->value);
  }

  if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
    print_error(_SLIT("Cannot modify readonly variable"));
    return NULL;
  }

  var->str = string__from(value);
  var->value.type = type;

  switch (type) {
    case VAR_STRING:
      var->value._str = string__from(value);
      process_string_variable(var);
      break;
    case VAR_INTEGER:
      StrconvResult result = ratoll(value.str, &var->value._number);
      if (result.is_err) {
        print_error(_SLIT("Failed to convert string to integer"));
        return NULL;
      }
      break;
    case VAR_ARRAY:
      if (var->value._array.data) array_free(&var->value._array);
      parse_and_set_array(variable_table, name, value);
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      if (var->value._map) map_free(var->value._map);
      parse_and_set_associative_array(variable_table, name, value);
      break;
    default:
      break;
  }
  if (readonly) set_variable_flag(&var->flags, VarFlag_ReadOnly);
  process_exported_variable(var);
  return var;
}

Variable* get_variable(VariableTable* table, const string name) {
  for (int i = 0; i < table->size; i++) {
    if (string__compare(table->variables[i].name, name) == 0) {
      Variable* var = &table->variables[i];
      if (var->value.type == VAR_NAMEREF) {
        Variable* resolved = resolve_nameref(var);
        if (resolved == NULL) {
          print_error(_SLIT("Failed to resolve nameref"));
          return NULL;
        }
        return resolved;
      }
      return var;
    }
  }
  return NULL;
}

void unset_variable(VariableTable* table, const string name) {
  for (int i = 0; i < table->size; i++) {
    if (string__compare(table->variables[i].name, name) == 0) {
      if (is_variable_flag_set(&table->variables[i].flags, VarFlag_ReadOnly)) {
        print_error(_SLIT("Cannot unset readonly variable"));
        return;
      }
      string__free(table->variables[i].name);
      string__free(table->variables[i].str);
      free_va_value(&table->variables[i].value);
      memmove(&table->variables[i], &table->variables[i + 1], (size_t)((table->size - i - 1) * (int)sizeof(Variable)));
      table->size--;
      return;
    }
  }
}

void parse_and_set_array(VariableTable* table, const string name, const string value) {
  string trimmed_value = string__substring(value, 1, (ssize_t)(value.len - 1));
  Variable* var = create_new_variable(table, name, VAR_ARRAY);

  char* token;
  char* rest = trimmed_value.str;

  while ((token = strtok_r(rest, " ", &rest)) != NULL) {
    string stroken = string__new(token);
    VariableType type = parse_variable_type(stroken);
    va_value_t _value = string_to_va_value(stroken, type);
    array_push(&var->value._array, &_value);
    string__free(stroken);
  }

  string__free(trimmed_value);
}

void parse_and_set_associative_array(VariableTable* table, const string name, const string input) {
  if (string__is_null_or_empty(input) || table == NULL) return;
  Variable* var = create_new_variable(table, name, VAR_ASSOCIATIVE_ARRAY);

  if (input.str[0] != '{' || input.str[input.len - 1] != '}') {
    fprintf(stderr, "Invalid input format\n");
    return;
  }

  string trimmed_input = string__substring(input, 1, (ssize_t)(input.len - 1));

  char* token = strtok(trimmed_input.str, " ");
  while (token != NULL) {
    char* equal_sign = strchr(token, '=');
    if (equal_sign != NULL) {
      *equal_sign = '\0';
      if (token[0] != '[') {
        fprintf(stderr, "Invalid key format\n");
        string__free(trimmed_input);
        return;
      }
      char* key = token + 1;
      char* value = equal_sign + 1;
      char* close_bracket = strchr(key, ']');
      if (close_bracket != NULL)
        *close_bracket = '\0';
      string skey = string__new(key);
      string svalue = string__new(value);
      set_associative_array_variable(table, var->name, skey, svalue);
      string__free(skey);
    }
    token = strtok(NULL, " ");
  }
  
  string__free(trimmed_input);
}

void array_set_element(VariableTable* table, const string name, size_t index, const string value) {
  Variable* var = get_variable(table, name);
  if (var == NULL || var->value.type != VAR_ARRAY) {
    print_error(_SLIT("Variable is not an array"));
    return;
  }

  if (index >= var->value._array.size) {
    print_error(_SLIT("Index out of bounds"));
    return;
  }

  VariableType type = parse_variable_type(value);
  va_value_t new_value = string_to_va_value(value, type);
  array_index_set(&var->value._array, index, &new_value);
}

bool do_not_expand_this_builtin(const string name) {
  string do_not_expand[] = {_SLIT("readonly"), _SLIT("set"), _SLIT("unset"), _SLIT("declare")};
  for (int i = 0; (unsigned long)i < sizeof(do_not_expand) / sizeof(string); i++) {
    if (string__compare(name, do_not_expand[i]) == 0) {
      return true;
    }
  }
  return false;
}

VariableType parse_variable_type(string value) {
  if (string__is_null_or_empty(value))
    return VAR_STRING;
  rstring__trim(value);
  if (string__is_null_or_empty(value))
    return VAR_STRING;
  if ((string__startswith(value, _SLIT("\"")) && string__endswith(value, _SLIT("\""))) ||
      (string__startswith(value, _SLIT("'")) && string__endswith(value, _SLIT("'"))))
    return VAR_STRING;
  if (string__isdigit(value))
    return VAR_INTEGER;
  if (string__startswith(value, _SLIT("(")) && string__endswith(value, _SLIT(")")))
    return VAR_ARRAY;
  if (string__startswith(value, _SLIT("{")) && string__endswith(value, _SLIT("}")))
    return VAR_ASSOCIATIVE_ARRAY;
  return VAR_STRING;
}

Variable* resolve_nameref(Variable* var) {
  if (var == NULL || var->value.type != VAR_NAMEREF) {
    return var;
  }
  
  Variable* resolved = var;
  int depth = 0;
  while (resolved != NULL && resolved->value.type == VAR_NAMEREF) {
    if (depth++ > 100) {
      print_error(_SLIT("Too many levels of indirection"));
      return NULL;
    }
    resolved = get_variable(variable_table, resolved->value._str);
    if (resolved == NULL) {
      print_error(_SLIT("Variable not found"));
      return NULL;
    }
    if (resolved->value.type != VAR_NAMEREF) break;
  }
  return resolved;
}

void set_associative_array_variable(VariableTable* table, const string name, const string key, const string value) {
  Variable* var = get_variable(table, name);
  if (var == NULL) var = create_new_variable(table, name, VAR_ASSOCIATIVE_ARRAY);

  if (var->value.type != VAR_ASSOCIATIVE_ARRAY) {
    print_error(_SLIT("Variable is not an associative array"));
    return;
  }

  va_value_t outval;
  size_t outval_size;
  MapResult result = map_get(var->value._map, key.str, &outval, &outval_size);
  if (!result.is_err) {
    free_va_value(&outval);
  }

  VariableType vt = parse_variable_type(value);
  va_value_t new_value = string_to_va_value(value, vt);
  map_insert(var->value._map, key.str, &new_value, sizeof(va_value_t));
}

string va_value_to_string(const va_value_t* value) {
  string result = _SLIT0;
  switch (value->type) {
    case VAR_STRING:
      result = string__from(value->_str);
      break;
    case VAR_INTEGER:
      char* temp = rmalloc(32);
      snprintf(temp, 32, "%lld", value->_number);
      result = string__new(temp);
      rfree(temp);
      break;
    case VAR_ARRAY: {
      StringBuilder sb = string_builder__new();
      string_builder__append_char(&sb, '(');
      for (size_t i = 0; i < value->_array.size; ++i) {
        va_value_t element = *(va_value_t*)array_checked_get(value->_array, i);
        string element_str = va_value_to_string(&element);
        string_builder__append(&sb, element_str);
        string__free(element_str);
        if (i < value->_array.size - 1)
          string_builder__append_char(&sb, ' ');
      }
      string_builder__append_char(&sb, ')');
      result = string_builder__to_string(&sb);
      string_builder__free(&sb);
      break;
    }
    case VAR_ASSOCIATIVE_ARRAY: {
      StringBuilder sb = string_builder__new();
      MapIterator it = map_iterator(value->_map);
      string_builder__append_char(&sb, '{');
      while (map_has_next(&it)) {
        const char* key = map_next(&it);
        va_value_t* element = map_iterator_get_value(&it, NULL);
        string element_str = va_value_to_string(element);
        string_builder__append_char(&sb, '[');
        string_builder__append_cstr(&sb, key);
        string_builder__append_char(&sb, ']');
        string_builder__append_char(&sb, '=');
        string_builder__append(&sb, element_str);
        if (map_has_next(&it))
          string_builder__append_char(&sb, ' ');
        string__free(element_str);
      }
      string_builder__append_char(&sb, '}');
      result = string_builder__to_string(&sb);
      string_builder__free(&sb);
      break;
    }
    default:
      result = _SLIT0;
      break;
  }
  return result;
}

va_value_t string_to_va_value(const string str, VariableType type) {
  va_value_t result;
  result.type = type;
  switch (type) {
    case VAR_STRING:
      result._str = string__remove_quotes(str);
      break;
    case VAR_INTEGER:
      result._number = atoll(str.str);
      break;
    case VAR_ARRAY: {
      result._array = create_array(sizeof(va_value_t));
      char* token;
      char* rest = rstrdup(str.str + 1);
      rest[strlen(rest) - 1] = '\0';
      while ((token = strtok_r(rest, " ", &rest))) {
        string stroken = string__new(token);
        VariableType _type = parse_variable_type(stroken);
        va_value_t value = string_to_va_value(stroken, _type);
        string__free(stroken);
        array_push(&result._array, &value);
      }
      rfree(rest);
      break;
    }
    case VAR_ASSOCIATIVE_ARRAY: {
      result._map = create_map_with_func(vfree_va_value);
      string trimmed_input = string__substring(str, 1, (ssize_t)(str.len - 1));
      char* token = strtok(trimmed_input.str, " ");
      while (token != NULL) {
        char* equal_sign = strchr(token, '=');
        if (equal_sign != NULL) {
          *equal_sign = '\0';
          if (token[0] != '[') {
            fprintf(stderr, "Invalid key format\n");
            string__free(trimmed_input);
            result.type = VAR_STRING;
            result._str = _SLIT0;
            break;
          }
          char* key = token + 1;
          char* value = equal_sign + 1;
          char* close_bracket = strchr(key, ']');
          if (close_bracket != NULL)
            *close_bracket = '\0';
          string svalue = string__new(value);
          VariableType vt = parse_variable_type(svalue);
          va_value_t new_value = string_to_va_value(svalue, vt);
          map_insert(result._map, key, &new_value, sizeof(va_value_t));
          string__free(svalue);
        }
        token = strtok(NULL, " ");
      }
      string__free(trimmed_input);
      break;
    }
    default:
      result.type = VAR_STRING;
      result._str = _SLIT0;
      break;
  }
  return result;
}

void free_va_value(va_value_t* value) {
  if (value == NULL) return;
  switch (value->type) {
    case VAR_STRING:
      string__free(value->_str);
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
      string__free(value->_str);
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
  string__free(var->name);
  string__free(var->str);
  free_va_value(&var->value);
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

            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var && var->value.type == VAR_ASSOCIATIVE_ARRAY) {
              va_value_t value;
              size_t value_size;
              MapResult map_result = map_get(var->value._map, key, &value, &value_size);
              if (!map_result.is_err) {
                string str_value = va_value_to_string(&value);
                dynstrcpy(&result, &result_size, &result_len, str_value.str);
                string__free(str_value);
              }
            } else if (var && var->value.type == VAR_ARRAY) {
              long long index;
              StrconvResult sres = ratoll(key, &index);
              if (sres.is_err) {
                print_error(_SLIT("Invalid key for associative array"));
                return NULL;
              }
              if (index >= 0 && index < (long long)var->value._array.size) {
                va_value_t* value = array_checked_get(var->value._array, (size_t)index);
                string str_value = va_value_to_string(value);
                dynstrcpy(&result, &result_size, &result_len, str_value.str);
                string__free(str_value);
              }
            }
          } else if (hash) {
            if (*var_name == '#') {
              const char* vname = hash + 1;
              string svname = string__new(vname);
              Variable* var = get_variable(table, svname);
              string__free(svname);
              if (var) {
                size_t length = var->str.len;
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
                
                string svarname = string__new(var_name);
                Variable* var = get_variable(table, svarname);
                string__free(svarname);
                if (var) {
                  string value = string__from(var->str);
                  string spattern = string__new(pattern);
                  string new_value = string__remove_prefix(value, spattern, is_longest_match);

                  dynstrcpy(&result, &result_size, &result_len, new_value.str);

                  string__free(value);
                  string__free(spattern);
                  string__free(new_value);
                }
              }
            }
          } else if (exclamation) {
            char* indirect_var_name = var_name + 1;
            size_t indirect_var_name_len = strlen(indirect_var_name);
            if (indirect_var_name[indirect_var_name_len - 1] == '*' || indirect_var_name[indirect_var_name_len - 1] == '@') {
              indirect_var_name[indirect_var_name_len - 1] = '\0';
              string sindirect_var_name = string__new(indirect_var_name);
              for (int i = 0; i < table->size; i++) {
                if (string__compare(table->variables[i].name, sindirect_var_name) == 0) {
                  dynstrcpy(&result, &result_size, &result_len, table->variables[i].name.str);
                  dynstrcpy(&result, &result_size, &result_len, " ");
                }
              }
              string__free(sindirect_var_name);
              if (result_len > 0 && result[result_len - 1] == ' ')
                result_len--;
            } else {
              string sindirect_var_name = string__new(indirect_var_name);
              Variable* indirect_var = get_variable(table, sindirect_var_name);
              if (indirect_var) {
                Variable* target_var = get_variable(table, indirect_var->str);
                if (target_var) {
                  dynstrcpy(&result, &result_size, &result_len, target_var->str.str);
                }
              }
              string__free(sindirect_var_name);
            }
          } else if (caret) {
            *caret = '\0';
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              char* value = rstrdup(var->str.str);
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
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              char* value = rstrdup(var->str.str);
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
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              char* endptr;
              long offset = strtol(colon + 1, &endptr, 10);
              char* length_str = (*endptr == ':') ? endptr + 1 : NULL;

              size_t var_len = strlen(var->str.str);
              
              if (offset < 0) {
                offset = (long)var_len + offset;
              }

              if (length_str) {
                long length = strtol(length_str, NULL, 10);
                if (offset >= 0 && length > 0 && offset + length <= (long)var_len) {
                  char* temp = rmalloc((size_t)length + 1);
                  strncpy(temp, var->str.str + offset, (size_t)length);
                  temp[length] = '\0';
                  dynstrcpy(&result, &result_size, &result_len, temp);
                  rfree(temp);
                }
              } else {
                if (offset >= 0 && offset < (long)var_len) {
                  dynstrcpy(&result, &result_size, &result_len, var->str.str + offset);
                }
              }
            }
          } else if (at) {
            *at = '\0';
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              if (*(at + 1) == 'Q') {
                dynstrcpy(&result, &result_size, &result_len, "'");
                for (char* c = var->str.str; *c; c++) {
                  if (*c == '\'') {
                    dynstrcpy(&result, &result_size, &result_len, "'\\''");
                  } else {
                    char temp[2] = {*c, '\0'};
                    dynstrcpy(&result, &result_size, &result_len, temp);
                  }
                }
                dynstrcpy(&result, &result_size, &result_len, "'");
              } else if (*(at + 1) == 'E') {
                int temp = parse_and_execute(var->str);
                char exit_status[20];
                snprintf(exit_status, sizeof(exit_status), "%d", temp);
                dynstrcpy(&result, &result_size, &result_len, exit_status);
              }
            }
          } else if (colon && (*(colon + 1) == '-' || *(colon + 1) == '=' || *(colon + 1) == '+')) {
            *colon = '\0';
            char* value = colon + 2;
            char operation = *(colon + 1);

            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            if (operation == '+') {
              if (var && !string__is_null_or_empty(var->str) && var->str.len > 0) {
                dynstrcpy(&result, &result_size, &result_len, value);
              }
            } else {
              if (!var || !string__is_null_or_empty(var->str) || var->str.len == 0) {
                if (operation == '=') {
                  string svalue = string__new(value);
                  var = set_variable(table, svarname, svalue, parse_variable_type(svalue), false);
                  string__free(svalue);
                }
                dynstrcpy(&result, &result_size, &result_len, value);
              } else {
                dynstrcpy(&result, &result_size, &result_len, var->str.str);
              }
            }
            string__free(svarname);
          } else if (question) {
            *question = '\0';
            string error_message = string__new(question + 1);
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (!var || !string__is_null_or_empty(var->str) || var->str.len == 0) {
              print_error(error_message);
            } else {
              dynstrcpy(&result, &result_size, &result_len, var->str.str);
            }
            string__free(error_message);
          } else if (slash) {
            *slash = '\0';
            char* pattern = slash + 1;
            bool replace_all = (*pattern == '/');
            if (replace_all) pattern++;
            
            char* replacement = strchr(pattern, '/');
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (replacement) {
              *replacement = '\0';
              replacement++;
              
              if (var) {
                char* new_value = str_replace(var->str.str, pattern, replacement, replace_all);
                dynstrcpy(&result, &result_size, &result_len, new_value);
                rfree(new_value);
              }
            } else {
              if (var) {
                char* temp_result = rstrdup(var->str.str);
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
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              bool greedy = (*(percent + 1) == '%');
              if (greedy) pattern++;
              char* new_value = remove_suffix(var->str.str, pattern, greedy);
              dynstrcpy(&result, &result_size, &result_len, new_value);
              rfree(new_value);
            }
          } else {
            string svarname = string__new(var_name);
            Variable* var = get_variable(table, svarname);
            string__free(svarname);
            if (var) {
              dynstrcpy(&result, &result_size, &result_len, var->str.str);
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

        string svarname = string__new(var_name);
        Variable* var = get_variable(table, svarname);
        string__free(svarname);
        if (var) {
          if (var->value.type == VAR_ARRAY || var->value.type == VAR_ASSOCIATIVE_ARRAY) {
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

                switch (var->value.type) {
                  case VAR_ARRAY: {
                    long long index;
                    StrconvResult sres = ratoll(index_str, &index);
                    if (!sres.is_err && index >= 0 && index < (long long)var->value._array.size) {
                      va_value_t* value = array_checked_get(var->value._array, (size_t)index);
                      string str_value = va_value_to_string(value);
                      dynstrcpy(&result, &result_size, &result_len, str_value.str);
                      string__free(str_value);
                    }
                    break;
                  }
                  case VAR_ASSOCIATIVE_ARRAY: {
                    va_value_t value;
                    size_t value_size;
                    MapResult map_result = map_get(var->value._map, index_str, &value, &value_size);
                    if (!map_result.is_err) {
                      string str_value = va_value_to_string(&value);
                      dynstrcpy(&result, &result_size, &result_len, str_value.str);
                      string__free(str_value);
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
              string value = va_value_to_string(&var->value);
              dynstrcpy(&result, &result_size, &result_len, value.str);
              string__free(value);
            }
          } else {
            string value = va_value_to_string(&var->value);
            dynstrcpy(&result, &result_size, &result_len, value.str);
            string__free(value);
          }
        } else {
          dynstrcpy(&result, &result_size, &result_len, "");
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