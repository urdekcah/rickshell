#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "builtin.h"
#include "io.h"
#include "variable.h"
#include "memory.h"
#include "rstring.h"
#include "array.h"

extern VariableTable* variable_table;

int builtin_declare(Command *cmd) {
  if (cmd->argv.size < 2) {
    ffprintln(stderr, "declare: usage: declare [-aAilunrx] [name[=value] ...]");
    return 1;
  }

  bool set_array = false, set_assoc_array = false, set_integer = false,
     set_uppercase = false, set_lowercase = false, set_nameref = false,
     set_readonly = false, set_export = false;
  bool unset_mode = false;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    if (string__startswith(elem, _SLIT("-")) || string__startswith(elem, _SLIT("+"))) {
      unset_mode = (elem.str[0] == '+');
      for (size_t j = 1; elem.str[j]; j++) {
        switch (elem.str[j]) {
          case 'a': set_array = !unset_mode; break;
          case 'A': set_assoc_array = !unset_mode; break;
          case 'i': set_integer = !unset_mode; break;
          case 'u': set_uppercase = !unset_mode; break;
          case 'l': set_lowercase = !unset_mode; break;
          case 'n': set_nameref = !unset_mode; break;
          case 'r': set_readonly = !unset_mode; break;
          case 'x': set_export = !unset_mode; break;
          default:
            ffprintln(stderr, "declare: invalid option '%c'", elem.str[j]);
            return 1;
        }
      }
    } else {
      string _elem = *(string*)array_checked_get(cmd->argv, i);
      ssize_t equals_pos = string__indexof(_elem, _SLIT("="));
      if (equals_pos == -1) {
        ffprintln(stderr, "declare: invalid argument '%S'", _elem);
        return 1;
      }

      string name = string__substring(_elem, 0, equals_pos);
      string value = string__substring(_elem, equals_pos + 1);

      Variable *var = get_variable(variable_table, name);
      VariableType type = VAR_STRING;
      if ((set_array + set_assoc_array + set_integer + set_nameref + set_uppercase + set_lowercase) > 0) {
        if (set_integer) type = VAR_INTEGER;
        if (set_uppercase || set_lowercase) type = VAR_STRING;
        if (set_array) type = VAR_ARRAY;
        if (set_assoc_array) type = VAR_ASSOCIATIVE_ARRAY;
        if (set_nameref) type = VAR_NAMEREF;
      } else {
        if (!string__is_null_or_empty(value)) type = parse_variable_type(value);
      }

      if (type == VAR_NAMEREF && string__is_null_or_empty(value)) {
        ffprintln(stderr, "declare: nameref requires a value");
        return 1;
      }

      if (var == NULL) var = create_new_variable(variable_table, name, type);
      if (var == NULL) {
        ffprintln(stderr, "declare: failed to create variable");
        return 1;
      }

      if (type == VAR_STRING) {
        if (set_uppercase) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Uppercase);
        else if (set_lowercase) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Lowercase);
        set_variable(variable_table, name, value, type, false);
      } else if (type == VAR_NAMEREF) {
        var->value._str = string__from(value);
        if (get_variable(variable_table, value) == NULL)
          set_variable(variable_table, value, _SLIT(""), VAR_STRING, false);
      } else {
        VariableType vt = parse_variable_type(value);
        if (vt != type) {
          ffprintln(stderr, "declare: type does not match");
          return 1;
        }
        set_variable(variable_table, name, value, type, false);
      }
      if (set_readonly) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_ReadOnly);
      if (set_export) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Exported);
      process_exported_variable(var);
      string__free(name);
    }
  }

  return 0;
}