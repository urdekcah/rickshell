#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "variable.h"
#include "builtin.h"
#include "error.h"
#include "expr.h"
#include "rstring.h"
#include "array.h"
#include "io.h"

extern VariableTable* variable_table;

int builtin_unset(Command* cmd) {
  if (cmd == NULL || cmd->argv.size == 0) {
    return -1;
  }

  bool _unset_variable = true;
  bool unset_nameref = false;
  int exit_status = 0;
  size_t i;

  for (i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    if (elem.str[0] != '-') {
      break;
    }
    for (int j = 1; elem.str[j]; j++) {
      switch (elem.str[j]) {
        case 'v':
          _unset_variable = true;
          break;
        case 'n':
          unset_nameref = true;
          break;
        default:
          ffprintln(stderr, "unset: invalid option -%c", elem.str[j]);
          return 1;
      }
    }
  }

  for (; i < cmd->argv.size; i++) {
    string name = *(string*)array_checked_get(cmd->argv, i);
    Variable* var = get_variable(variable_table, name);
    if (var) {
      if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
        ffprintln(stderr, "unset: cannot unset \"%S\". readonly variable", name);
        exit_status = 1;
      } else if (unset_nameref && is_variable_flag_set(&var->flags, VarFlag_Referece)) {
        unset_variable_flag(&var->flags, VarFlag_Referece);
      } else if (_unset_variable) {
        unset_variable(variable_table, name);
      }
    } else {
      ffprintln(stderr, "unset: cannot unset \"%S\". variable is not set", name);
      exit_status = 1;
    }
  }

  return exit_status;
}