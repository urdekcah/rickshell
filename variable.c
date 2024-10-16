#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "variable.h"
#include "memory.h"
#include "io.h"
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
    if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
      print_error(_SLIT("Cannot modify readonly variable"));
      return NULL;
    }
    string__free(var->str);
    free_va_value(&var->value);
  }

  var->str = string__from(value);
  var->value.type = type;

  switch (type) {
    case VAR_STRING:
      var->value._str = string__remove_quotes(value);
      string__free(var->str);
      var->str = string__from(var->value._str);
      process_string_variable(var);
      break;
    case VAR_INTEGER:
      StrconvResult result = ratoll(value, &var->value._number);
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
      var->value._map = create_map_with_func(vfree_va_value);
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
  Variable* existing = get_variable(table, name);
  if (existing != NULL) {
    if (is_variable_flag_set(&existing->flags, VarFlag_ReadOnly)) {
      print_error(_SLIT("Cannot modify readonly variable"));
      return;
    }
    unset_variable(table, name);
  }
  Variable* var = create_new_variable(table, name, VAR_ASSOCIATIVE_ARRAY);

  if (input.str[0] != '{' || input.str[input.len - 1] != '}') {
    ffprintln(stderr, "Invalid input format");
    return;
  }

  string trimmed_input = string__substring(input, 1, (ssize_t)(input.len - 1));

  char* token = strtok(trimmed_input.str, " ");
  while (token != NULL) {
    char* equal_sign = strchr(token, '=');
    if (equal_sign != NULL) {
      *equal_sign = '\0';
      if (token[0] != '[') {
        ffprintln(stderr, "Invalid key format");
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
      string__free(svalue);
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
  string do_not_expand[] = {_SLIT("declare"), _SLIT("export"), _SLIT("readonly"), _SLIT("set"), _SLIT("unset")};
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

  VariableType vt = parse_variable_type(value);
  va_value_t new_value = string_to_va_value(value, vt);
  map_insert(var->value._map, key.str, &new_value, sizeof(va_value_t));
}

string va_value_default_string(const VariableType type) {
  string result = _SLIT0;
  switch (type) {
    case VAR_STRING:
      result = _SLIT("\"\"");
      break;
    case VAR_INTEGER:
      result = _SLIT("0");
      break;
    case VAR_ARRAY:
      result = _SLIT("()");
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      result = _SLIT("{}");
      break;
    default:
      result = _SLIT0;
      break;
  }
  return result;
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
            ffprintln(stderr, "Invalid key format");
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

string expand_variables(VariableTable* table, const string input) {
  StringBuilder sb = string_builder__new();
  ssize_t p = 0;

  while (p < (ssize_t)input.len) {
    if (input.str[p] == '$') {
      if (p + 1 < (ssize_t)input.len && input.str[p + 1] == '{') {
        ssize_t end;
        {
          string temp = string__substring(input, p + 2);
          end = string__indexof(temp, _SLIT("}"));
          string__free(temp);
        }
        if (end != -1) {
          end += p + 2; 
          string var_name = string__substring(input, p + 2, end);

          string open_bracket = _SLIT("[");
          string close_bracket = _SLIT("]");
          string colon = _SLIT(":");
          string hash = _SLIT("#");
          string slash = _SLIT("/");
          string percent = _SLIT("%");
          string question = _SLIT("?");
          string exclamation = _SLIT("!");
          string caret = _SLIT("^");
          string comma = _SLIT(",");
          string at = _SLIT("@");

          ssize_t open_bracket_pos = string__indexof(var_name, open_bracket);
          ssize_t close_bracket_pos = string__lastindexof(var_name, close_bracket);
          ssize_t colon_pos = string__indexof(var_name, colon);
          ssize_t hash_pos = string__indexof(var_name, hash);
          ssize_t slash_pos = string__indexof(var_name, slash);
          ssize_t percent_pos = string__indexof(var_name, percent);
          ssize_t question_pos = string__indexof(var_name, question);
          ssize_t exclamation_pos = string__indexof(var_name, exclamation);
          ssize_t caret_pos = string__indexof(var_name, caret);
          ssize_t comma_pos = string__indexof(var_name, comma);
          ssize_t at_pos = string__indexof(var_name, at);

          if (open_bracket_pos != -1 && close_bracket_pos != -1 && open_bracket_pos < close_bracket_pos) {
            string name = string__substring(var_name, 0, open_bracket_pos);
            string key = string__substring(var_name, open_bracket_pos + 1, close_bracket_pos);

            Variable* var = get_variable(table, name);
            if (var && var->value.type == VAR_ASSOCIATIVE_ARRAY) {
              va_value_t value;
              size_t value_size;
              MapResult map_result = map_get(var->value._map, key.str, &value, &value_size);
              if (!map_result.is_err) {
                string str_value = va_value_to_string(&value);
                string_builder__append(&sb, str_value);
                string__free(str_value);
              }
            } else if (var && var->value.type == VAR_ARRAY) {
              long long index;
              StrconvResult sres = ratoll(key, &index);
              if (!sres.is_err) {
                if (index >= 0 && index < (long long)var->value._array.size) {
                  va_value_t* value = array_checked_get(var->value._array, (size_t)index);
                  string str_value = va_value_to_string(value);
                  string_builder__append(&sb, str_value);
                  string__free(str_value);
                }
              }
            }
            string__free(name);
            string__free(key);
          } else if (hash_pos != -1) {
            if (var_name.str[0] == '#') {
              string vname = string__substring(var_name, hash_pos + 1);
              Variable* var = get_variable(table, vname);
              string__free(vname);
              if (var) {
                size_t length = string__length(var->str);
                char length_str[20];
                snprintf(length_str, sizeof(length_str), "%zu", length);
                string_builder__append_cstr(&sb, length_str);
              }
            } else {
              string prefix = string__substring(var_name, 0, hash_pos);
              string pattern = string__substring(var_name, hash_pos + 1);
              
              bool is_longest_match = (pattern.str[0] == '#');
              if (is_longest_match) {
                pattern = string__substring(pattern, 1);
              }
              
              Variable* var = get_variable(table, prefix);
              if (var) {
                string value = string__from(var->str);
                string new_value = string__remove_prefix(value, pattern, is_longest_match);
                string_builder__append(&sb, new_value);
                string__free(value);
                string__free(new_value);
              }
            }
          } else if (exclamation_pos != -1) {
            string indirect_var_name = string__substring(var_name, 1);
            if (indirect_var_name.str[indirect_var_name.len - 1] == '*' || indirect_var_name.str[indirect_var_name.len - 1] == '@') {
              {
                string temp = string__substring(indirect_var_name, 0, (ssize_t)indirect_var_name.len - 1);
                string__free(indirect_var_name);
                indirect_var_name = temp;
              }
              for (int i = 0; i < table->size; i++) {
                string temp = string__from(table->variables[i].name);
                if (string__startswith(temp, indirect_var_name)) {
                  string_builder__append(&sb, temp);
                  string_builder__append_char(&sb, ' ');
                }
                string__free(temp);
              }
              if (sb.len > 0 && sb.buffer[sb.len - 1] == ' ') {
                sb.len--;
              }
            } else {
              Variable* indirect_var = get_variable(table, indirect_var_name);
              if (indirect_var) {
                Variable* target_var = get_variable(table, indirect_var->str);
                if (target_var) {
                  string_builder__append(&sb, target_var->str);
                }
              }
            }
            string__free(indirect_var_name);
          } else if (caret_pos != -1) {
            string name = string__substring(var_name, 0, caret_pos);
            string pattern = string__substring(var_name, caret_pos);
            Variable* var = get_variable(table, name);
            string__free(name);
            if (var) {
              string value = string__from(var->str);
              bool convert_all = (pattern.str[0] == '^' && pattern.len > 1 && pattern.str[1] == '^');
              {
                string temp = string__substring(pattern, (convert_all) ? 2 : 1);
                string__free(pattern);
                pattern = temp;
              }
        
              if (pattern.len == 0) {
                for (size_t i = 0; i < value.len; i++)
                  if (convert_all || (!convert_all && i == 0)) value.str[i] = (char)toupper(value.str[i]);
              } else {
                bool first_found = false;
                for (size_t i = 0; i < value.len; i++) {
                  string temp = string__substring(value, (ssize_t)i, (ssize_t)i+1);
                  if (string__contains(pattern, temp)) {
                    if (convert_all || (!convert_all && !first_found)) {
                      value.str[i] = (char)toupper(value.str[i]);
                      if (!first_found) first_found = true;
                    }
                  }
                  string__free(temp);
                }
              }
              string_builder__append(&sb, value);
              string__free(value);
            }
            string__free(pattern);
          } else if (comma_pos != -1) {
            string name = string__substring(var_name, 0, comma_pos);
            string pattern = string__substring(var_name, comma_pos);
            Variable* var = get_variable(table, name);
            string__free(name);
            if (var) {
              string value = string__from(var->str);
              bool convert_all = (pattern.str[0] == ',' && pattern.len > 1 && pattern.str[1] == ',');
              {
                string temp = string__substring(pattern, (convert_all) ? 2 : 1);
                string__free(pattern);
                pattern = temp;
              }
        
              if (pattern.len == 0) {
                for (size_t i = 0; i < value.len; i++)
                  if (convert_all || (!convert_all && i == 0)) value.str[i] = (char)tolower(value.str[i]);
              } else {
                bool first_found = false;
                for (size_t i = 0; i < value.len; i++) {
                  string temp = string__substring(value, (ssize_t)i, (ssize_t)i+1);
                  if (string__contains(pattern, temp)) {
                    if (convert_all || (!convert_all && !first_found)) {
                      value.str[i] = (char)tolower(value.str[i]);
                      if (!first_found) first_found = true;
                    }
                  }
                  string__free(temp);
                }
              }
              string_builder__append(&sb, value);
              string__free(value);
            }
            string__free(pattern);
          } else if (colon_pos != -1 && !(var_name.str[colon_pos + 1] == '?' || var_name.str[colon_pos + 1] == '-' || var_name.str[colon_pos + 1] == '=' || var_name.str[colon_pos + 1] == '+')) {
            string name = string__substring(var_name, 0, colon_pos);
            string offset_str = string__substring(var_name, colon_pos + 1);
            Variable* var = get_variable(table, name);
            string__free(name);
            if (var) {
              long offset;
              char* endptr;
              offset = strtol(offset_str.str, &endptr, 10);
              string _endptr = string__new(endptr);
              string length_str = (endptr[0] == ':') ? string__substring(_endptr, 1) : _SLIT0;
              string__free(_endptr);

              size_t var_len = string__length(var->str);
              
              if (offset < 0) {
                offset = (long)var_len + offset;
              }

              if (length_str.len > 0) {
                long length = strtol(length_str.str, NULL, 10);
                if (offset >= 0 && length > 0 && offset + length <= (long)var_len) {
                  string temp = string__substring(var->str, offset, offset + length);
                  string_builder__append(&sb, temp);
                  string__free(temp);
                }
              } else {
                if (offset >= 0 && offset < (long)var_len) {
                  string temp = string__substring(var->str, offset);
                  string_builder__append(&sb, temp);
                  string__free(temp);
                }
              }
              string__free(length_str);
            }
            string__free(offset_str);
          } else if (at_pos != -1) {
            string name = string__substring(var_name, 0, at_pos);
            Variable* var = get_variable(table, name);
            string__free(name);
            if (var) {
              if (var_name.str[at_pos + 1] == 'Q') {
                string_builder__append_char(&sb, '\'');
                for (size_t i = 0; i < var->str.len; i++) {
                  if (var->str.str[i] == '\'') {
                    string_builder__append_cstr(&sb, "'\\''");
                  } else {
                    string_builder__append_char(&sb, var->str.str[i]);
                  }
                }
                string_builder__append_char(&sb, '\'');
              } else if (var_name.str[at_pos + 1] == 'E') {
                print_error(_SLIT("Not implemented"));
              }
            }
          } else if (colon_pos != -1 && (var_name.str[colon_pos + 1] == '-' || var_name.str[colon_pos + 1] == '=' || var_name.str[colon_pos + 1] == '+')) {
            string name = string__substring(var_name, 0, colon_pos);
            string value = string__substring(var_name, colon_pos + 2);
            char operation = var_name.str[colon_pos + 1];

            Variable* var = get_variable(table, name);
            if (operation == '+') {
              if (var && var->str.len > 0) {
                string_builder__append(&sb, value);
              }
            } else {
              if (!var || var->str.len == 0) {
                if (operation == '=') {
                  var = set_variable(table, name, value, parse_variable_type(value), false);
                  string__free(value);
                  value = string__from(var->str);
                }
                string_builder__append(&sb, value);
              } else {
                string_builder__append(&sb, var->str);
              }
            }
            string__free(name);
            string__free(value);
          } else if (question_pos != -1) {
            string name = string__substring(var_name, 0, question_pos);
            string error_message = string__substring(var_name, question_pos + 1);

            Variable* var = get_variable(table, name);
            if (!var || var->str.len == 0) {
              string_builder__append(&sb, _SLIT("ERROR: "));
              string_builder__append(&sb, error_message);
            } else {
              string_builder__append(&sb, var->str);
            }
            string__free(name);
            string__free(error_message);
          } else if (slash_pos != -1) {
            string name = string__substring(var_name, 0, slash_pos);
            string pattern = string__substring(var_name, slash_pos + 1);
            bool replace_all = (pattern.str[0] == '/');
            if (replace_all) {
              string temp = string__substring(pattern, 1);
              string__free(pattern);
              pattern = temp;
            }
            
            ssize_t replacement_pos = string__indexof(pattern, slash);
            Variable* var = get_variable(table, name);
            string__free(name);
            if (replacement_pos != -1) {
              string replacement = string__substring(pattern, replacement_pos + 1);
              {
                string temp = string__substring(pattern, 0, replacement_pos);
                string__free(pattern);
                pattern = temp;
              }
              
              if (var) {
                string new_value = string__replace_all(var->str, pattern, replacement);
                if (!replace_all) {
                  string temp = string__replace(var->str, pattern, replacement);
                  string__free(new_value);
                  new_value = temp;
                }
                string_builder__append(&sb, new_value);
                string__free(new_value);
              }
              string__free(replacement);
            } else {
              if (var) {
                string temp_result = string__from(var->str);
                while (true) {
                  ssize_t pos = string__indexof(temp_result, pattern);
                  if (pos == -1) break;
                  string before = string__substring(temp_result, 0, pos);
                  string after = string__substring(temp_result, pos + (ssize_t)pattern.len);
                  string_builder__clear(&sb);
                  string_builder__append(&sb, before);
                  string_builder__append(&sb, after);
                  string__free(before);
                  string__free(after);
                  string__free(temp_result);
                  temp_result = string_builder__to_string(&sb);
                  string_builder__clear(&sb);
                  if (!replace_all) break;
                }
                string_builder__append(&sb, temp_result);
                string__free(temp_result);
              }
            }
            string__free(pattern);
          } else if (percent_pos != -1) {
            string name = string__substring(var_name, 0, percent_pos);
            string pattern = string__substring(var_name, percent_pos + 1);
            Variable* var = get_variable(table, name);
            if (var) {
              bool greedy = (pattern.str[0] == '%');
              if (greedy) {
                string new_pattern = string__substring(pattern, 1);
                string__free(pattern);
                pattern = new_pattern;
              }
              string new_value = string__remove_suffix(var->str, pattern, greedy);
              string_builder__append(&sb, new_value);
              string__free(new_value);
            }
            string__free(name);
            string__free(pattern);
          } else {
            Variable* var = get_variable(table, var_name);
            if (var) {
              string_builder__append(&sb, var->str);
            }
          }
          p = end + 1;
          string__free(var_name);
          continue;
        }
      } else if (p + 1 < (ssize_t)input.len && input.str[p + 1] == '?') {
        char exit_status[20];
        snprintf(exit_status, sizeof(exit_status), "%d", WEXITSTATUS(system(NULL)));
        string_builder__append_cstr(&sb, exit_status);
        p += 2;
        continue;
      } else if (p + 1 < (ssize_t)input.len && input.str[p + 1] == '!') {
        string_builder__append_cstr(&sb, "LAST_BG_PID");
        p += 2;
        continue;
      } else if (p + 1 < (ssize_t)input.len && (isalpha(input.str[p + 1]) || input.str[p + 1] == '_')) {
        ssize_t var_start = p + 1;
        ssize_t var_end = var_start;
        while (var_end < (ssize_t)input.len && (isalnum(input.str[var_end]) || input.str[var_end] == '_')) var_end++;
        
        string var_name = string__substring(input, var_start, var_end);

        Variable* var = get_variable(table, var_name);
        if (var) {
          if (var->value.type == VAR_ARRAY || var->value.type == VAR_ASSOCIATIVE_ARRAY) {
            ssize_t array_index_start;
            {
              string temp = string__substring(input, var_end);
              array_index_start = string__indexof(temp, _SLIT("["));
              string__free(temp);
            }
            if (array_index_start != -1) {
              array_index_start += var_end;
              ssize_t array_index_end;
              {
                string temp = string__substring(input, array_index_start);
                array_index_end = string__indexof(temp, _SLIT("]"));
                string__free(temp);
              }
              if (array_index_end != -1) {
                array_index_end += array_index_start;
                string index_str = string__substring(input, array_index_start + 1, array_index_end);
                string expanded_index = expand_variables(table, index_str);
                string__free(index_str);

                switch (var->value.type) {
                  case VAR_ARRAY: {
                    long long index;
                    StrconvResult sres = ratoll(expanded_index, &index);
                    if (!sres.is_err && index >= 0 && index < (long long)var->value._array.size) {
                      va_value_t* value = array_checked_get(var->value._array, (size_t)index);
                      string str_value = va_value_to_string(value);
                      string_builder__append(&sb, str_value);
                      string__free(str_value);
                    }
                    break;
                  }
                  case VAR_ASSOCIATIVE_ARRAY: {
                    va_value_t value;
                    size_t value_size;
                    MapResult map_result = map_get(var->value._map, expanded_index.str, &value, &value_size);
                    if (!map_result.is_err) {
                      string str_value = va_value_to_string(&value);
                      string_builder__append(&sb, str_value);
                      string__free(str_value);
                    }
                    break;
                  }
                  default:
                    break;
                }
                string__free(expanded_index);
                p = array_index_end + 1;
              } else {
                p = var_end;
              }
            } else {
              string value = va_value_to_string(&var->value);
              string_builder__append(&sb, value);
              string__free(value);
              p = var_end;
            }
          } else {
            string value = va_value_to_string(&var->value);
            string_builder__append(&sb, value);
            string__free(value);
            p = var_end;
          }
        } else {
          p = var_end;
        }
        string__free(var_name);
        continue;
      }
    }
    string_builder__append_char(&sb, input.str[p]);
    p++;
  }

  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}

bool is_variable_any_flag_set(va_flag_t* vf) { return *vf != 0; }
bool is_variable_flag_set(va_flag_t* vf, va_flag_t flag) { return (*vf & flag) != 0; }
void set_variable_flag(va_flag_t* vf, va_flag_t flag) { *vf |= flag; }
void unset_variable_flag(va_flag_t* vf, va_flag_t flag) { *vf &= ~flag; }