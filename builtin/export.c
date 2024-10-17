#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "builtin.h"
#include "variable.h"
#include "io.h"
#include "rstring.h"

extern VariableTable* variable_table;

int builtin_export(Command *cmd) {
  bool print_all = false;
  bool remove_export = false;
  size_t i;

  if (cmd->argv.size < 2) {
    print_all = true;
  }

  for (i = 1; i < cmd->argv.size; i++) {
    string arg = *(string*)array_checked_get(cmd->argv, i);

    if (string__equals(arg, _SLIT("--"))) {
      i++;
      break;
    }

    if (string__startswith(arg, _SLIT("-"))) {
      for (size_t j = 1; j < arg.len; j++) {
        switch (arg.str[j]) {
          case 'p':
            print_all = true;
            break;
          case 'n':
            remove_export = true;
            break;
          default:
            ffprintln(stderr, "export: invalid option: -%c", arg.str[j]);
            return 1;
        }
      }
    } else {
      break;
    }
  }

  if (print_all) {
    for (int j = 0; j < variable_table->size; j++) {
      Variable *var = &variable_table->variables[j];
      if (is_variable_flag_set(&var->flags, VarFlag_Exported))
        ffprintln(stdout, "declare -x %S=\"%S\"", var->name, var->str);
    }
    if (i == cmd->argv.size)
      return 0;
  }

  for (; i < cmd->argv.size; i++) {
    string arg = *(string*)array_checked_get(cmd->argv, i);
    ssize_t equals_pos = string__indexof(arg, _SLIT("="));
    Variable *var = NULL;

    if (equals_pos != -1) {
      string name = string__substring(arg, 0, equals_pos);
      string value = string__substring(arg, equals_pos + 1);
      var = set_variable(variable_table, name, value, VAR_STRING, false);
      if (var == NULL) {
        ffprintln(stderr, "export: unable to set variable: %S", name);
        string__free(name);
        return 1;
      }
      if (!remove_export) {
        set_variable_flag(&var->flags, VarFlag_Exported);
      }
      string__free(name);
    } else {
      var = get_variable(variable_table, arg);
      if (var == NULL) {
        if (!remove_export) {
          var = set_variable(variable_table, arg, _SLIT(""), VAR_STRING, false);
          if (var == NULL) {
            ffprintln(stderr, "export: unable to create variable: %S", arg);
            return 1;
          }
          set_variable_flag(&var->flags, VarFlag_Exported);
        }
      } else {
        (remove_export ? unset_variable_flag : set_variable_flag)(&var->flags, VarFlag_Exported);
      }
    }
    
    if (var != NULL) {
      process_exported_variable(var);
    }
  }

  return 0;
}