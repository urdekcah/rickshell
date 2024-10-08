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

extern VariableTable* variable_table;

int builtin_unset(Command* cmd) {
  if (cmd == NULL || cmd->argv.size == 0) {
    return -1;
  }

  bool _unset_variable = true; // need to change into false in future

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
        default:
          fprintf(stderr, "unset: invalid option -%c\n", elem.str[j]);
          return 1;
      }
    }
    string__free(elem);
  }

  int exit_status = 0;

  for (; i < cmd->argv.size; i++) {
    string name = *(string*)array_checked_get(cmd->argv, i);
    bool success = false;

    if (_unset_variable) {
      Variable* var = get_variable(variable_table, name);
      if (var) {
        if (is_variable_flag_set(&var->flags, VarFlag_ReadOnly)) {
          fprintf(stderr, "unset: %s: cannot unset: readonly variable\n", name.str);
          exit_status = 1;
        } else {
          unset_variable(variable_table, name);
          success = true;
        }
      }
    }

    if (!success) {
      printf("unset: \"%s\" not found\n", name.str);
    }
    string__free(name);
  }

  return exit_status;
}