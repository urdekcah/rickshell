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
#include "iterator.h"

#ifdef SREPLACE
  #undef SREPLACE
#endif
#define SREPLACE(v, s) { \
  string _##v = string__concat(v, s); \
  string__free(v); \
  v = _##v; \
}

extern VariableTable* variable_table;

static void print_variable(Variable* var) {
  if (!is_variable_any_flag_set(&var->flags)) return;
  string flags = _SLIT0;
  switch (var->value.type) {
    case VAR_INTEGER:
      flags = string__concat(flags, _SLIT("i"));
      break;
    case VAR_ARRAY:
      flags = string__concat(flags, _SLIT("a"));
      break;
    case VAR_ASSOCIATIVE_ARRAY:
      flags = string__concat(flags, _SLIT("A"));
      break;
    case VAR_NAMEREF:
      flags = string__concat(flags, _SLIT("n"));
      break;
    default:
      break;
  }
  if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) SREPLACE(flags, _SLIT("r"));
  if (is_variable_flag_set(&var->flags, VarFlag_Exported)) SREPLACE(flags, _SLIT("x"));
  if (is_variable_flag_set(&var->flags, VarFlag_Uppercase)) SREPLACE(flags, _SLIT("u"));
  if (is_variable_flag_set(&var->flags, VarFlag_Lowercase)) SREPLACE(flags, _SLIT("l"));

  ffprint(stdout, "declare -%S ", flags);

  ffprint(stdout, "%S", var->name);

  switch (var->value.type) {
    case VAR_INTEGER:
      ffprint(stdout, "=%lld", var->value._number);
      break;
    case VAR_ARRAY: {
      ffprint(stdout, "=(");
      for (int i = 0; i < var->array_size; i++) {
        if (i > 0) ffprint(stdout, " ");
        ffprint(stdout, "\"%S\"", ((string*)var->value._array.data)[i]);
      }
      ffprint(stdout, ")");
      break;
    }
    case VAR_ASSOCIATIVE_ARRAY: {
      ffprint(stdout, "={ ");
      MapIterator it = map_iterator(var->value._map);
      bool first = true;
      while (map_has_next(&it)) {
        const char* key = map_next(&it);
        size_t value_size;
        void* value = map_iterator_get_value(&it, &value_size);
        if (!first) ffprint(stdout, " ");
        ffprint(stdout, "[%s]=\"%S\"", key, *(string*)value);
        first = false;
      }
      ffprint(stdout, " }");
      break;
    }
    default:
      ffprint(stdout, "=\"%S\"", var->str);
      break;
  }

  ffprintln(stdout, "");
  string__free(flags);
}

int builtin_declare(Command *cmd) {
  if (cmd->argv.size < 2) {
    ffprintln(stderr, "declare: usage: declare [-aAilnprux] [-I] [name[=value] ...]");
    return 1;
  }

  bool set_array = false, set_assoc_array = false, set_integer = false,
     set_uppercase = false, set_lowercase = false, set_nameref = false,
     set_readonly = false, set_export = false, print_only = false, inherit = false;
  bool unset_mode = false;
  bool option_processing = true;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    
    if (option_processing && string__equals(elem, _SLIT("--"))) {
      option_processing = false;
      continue;
    }
    
    if (option_processing && (string__startswith(elem, _SLIT("-")) || string__startswith(elem, _SLIT("+")))) {
      unset_mode = (elem.str[0] == '+');
      for (size_t j = 1; elem.str[j]; j++) {
        switch (elem.str[j]) {
          case 'a': set_array = !unset_mode; break;
          case 'A': set_assoc_array = !unset_mode; break;
          case 'i': set_integer = !unset_mode; break;
          case 'u': set_uppercase = true; break;
          case 'l': set_lowercase = true; break;
          case 'n': set_nameref = !unset_mode; break;
          case 'r': set_readonly = true; break;
          case 'x': set_export = true; break;
          case 'p': print_only = true; break;
          case 'I': inherit = true; break;
          default:
            ffprintln(stderr, "declare: invalid option '%c'", elem.str[j]);
            return 1;
        }
      }

      if (print_only) {
        for (size_t j = 0; j < (size_t)variable_table->size; j++)
          print_variable(&variable_table->variables[j]);
        break;
      }
    } else {
      string _elem = elem;
      ssize_t equals_pos = string__indexof(_elem, _SLIT("="));
      string name, value;
      
      if (equals_pos == -1) {
        name = string__from(_elem);
        value = _SLIT0;
      } else {
        name = string__substring(_elem, 0, equals_pos);
        value = string__substring(_elem, equals_pos + 1);
      }

      Variable *var = get_variable(variable_table, name);
      VariableType type = VAR_STRING;

      if (var == NULL && inherit) {
        ffprintln(stderr, "declare: -I option not yet implemented");
        return 1;
      }

      if ((set_array + set_assoc_array + set_integer + set_nameref + set_uppercase + set_lowercase) > 0) {
        if (set_integer) type = VAR_INTEGER;
        if (set_uppercase || set_lowercase) type = VAR_STRING;
        if (set_array) type = VAR_ARRAY;
        if (set_assoc_array) type = VAR_ASSOCIATIVE_ARRAY;
        if (set_nameref) type = VAR_NAMEREF;
      } else if (!string__is_null_or_empty(value)) {
        type = parse_variable_type(value);
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

      if (!string__is_null_or_empty(value)) {
        switch (type) {
          case VAR_STRING:
            set_variable(variable_table, name, value, type, false);
            break;
          case VAR_NAMEREF:
            var->value._str = string__from(value);
            if (get_variable(variable_table, value) == NULL)
              set_variable(variable_table, value, _SLIT(""), VAR_STRING, false);
            break;
          case VAR_ARRAY:
            parse_and_set_array(variable_table, name, value);
            break;
          case VAR_ASSOCIATIVE_ARRAY:
            parse_and_set_associative_array(variable_table, name, value);
            break;
          default:
            VariableType vt = parse_variable_type(value);
            if (vt != type) {
              ffprintln(stderr, "declare: type does not match");
              return 1;
            }
            set_variable(variable_table, name, value, type, false);
            break;
        }
      }

      if (set_readonly) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_ReadOnly);
      if (set_export) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Exported);
      if (set_uppercase) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Uppercase);
      if (set_lowercase) ((unset_mode)?unset_variable_flag:set_variable_flag)(&var->flags, VarFlag_Lowercase);
      if (set_export) process_exported_variable(var);
      
      string__free(name);
      string__free(value);
    }
  }

  return 0;
}

#undef SREPLACE