#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"
#include "variable.h"
#include "error.h"
#include "expr.h"
#include "memory.h"
#include "rstring.h"
#include "array.h"
#include "io.h"

extern VariableTable* variable_table;

int builtin_readonly(Command *cmd) {
  bool display_all = false;
  bool refer_to_indexed_array = false;
  bool refer_to_associative_array = false;
  int option_end = 1;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    if (elem.str[0] == '-' && elem.str[1] != '-') {
      for (size_t j = 1; elem.str[j] != '\0'; j++) {
        switch (elem.str[j]) {
          case 'p':
            display_all = true;
            break;
          case 'a':
            refer_to_indexed_array = true;
            break;
          case 'A':
            refer_to_associative_array = true;
            break;
          default:
            print_error(_SLIT("Invalid option"));
            return 1;
        }
      }
      option_end = (int)i + 1;
    } else if (string__compare(elem, _SLIT("--")) == 0) {
      option_end = (int)i + 1;
      break;
    } else {
      break;
    }
  }

  if (display_all) {
    for (int i = 0; i < variable_table->size; i++) {
      Variable *var = &variable_table->variables[i];
      if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
        string value = va_value_to_string(&var->value);
        fprintln("readonly %S=%S", var->name, value);
        string__free(value);
      }
    }
    return 0;
  }

  for (size_t i = (size_t)option_end; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    ssize_t equals_pos = string__indexof(elem, _SLIT("=")); 
    string name = _SLIT0, value = _SLIT0;
    if (equals_pos != -1) {
      name = string__substring(elem, 0, equals_pos);
      value = string__substring(elem, equals_pos + 1);
    } else {
      name = string__from(elem);
    }
    
    Variable *var = get_variable(variable_table, name);
    if (var) {
      if (!string__is_null_or_empty(value)) {
        if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
          print_error(_SLIT("Cannot modify readonly variable"));
          return 1;
        }
        VariableType new_type = parse_variable_type(value);
        if (var->value.type != new_type) {
          print_error(_SLIT("Type mismatch: cannot change variable type"));
          return 1;
        }
        var->str = string__from(value);
      }
      set_variable_flag(&var->flags, VarFlag_ReadOnly);
    } else {
      VariableType type = VAR_STRING;
      
      if (refer_to_indexed_array) {
        type = VAR_ARRAY;
      } else if (refer_to_associative_array) {
        type = VAR_ASSOCIATIVE_ARRAY;
      }
      
      if (!string__is_null_or_empty(value)) {
        VariableType value_type = parse_variable_type(value);
        if ((refer_to_indexed_array && value_type != VAR_ARRAY) ||
            (refer_to_associative_array && value_type != VAR_ASSOCIATIVE_ARRAY)) {
          print_error(_SLIT("Type mismatch: value does not match specified array type"));
          return 1;
        }
        type = value_type;
      }
      
      var = set_variable(variable_table, name, (string__is_null_or_empty(value) ? va_value_default_string(type) : value), type, true);
      if (var == NULL) {
        print_error(_SLIT("Failed to set readonly variable"));
        return 1;
      }
    }
    
    string__free(name);
    string__free(value);
  }

  return 0;
}