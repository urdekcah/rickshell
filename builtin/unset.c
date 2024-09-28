#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "variable.h"
#include "builtin.h"
#include "error.h"
#include "expr.h"

extern VariableTable* variable_table;

int builtin_unset(Command* cmd) {
  if (cmd == NULL || cmd->argv.data == NULL) {
    return -1;
  }

  bool _unset_variable = true; // need to change into false in future

  size_t i;
  for (i = 1; i < cmd->argv.size; i++) {
    if (cmd->argv.data[i][0] != '-') {
      break;
    }
    for (int j = 1; cmd->argv.data[i][j]; j++) {
      switch (cmd->argv.data[i][j]) {
        case 'v':
          _unset_variable = true;
          break;
        default:
          fprintf(stderr, "unset: invalid option -%c\n", cmd->argv.data[i][j]);
          return 1;
      }
    }
  }

  int exit_status = 0;

  for (; i < cmd->argv.size; i++) {
    const char* name = cmd->argv.data[i];
    bool success = false;

    if (_unset_variable) {
      Variable* var = get_variable(variable_table, name);
      if (var) {
        if (var->readonly) {
          fprintf(stderr, "unset: %s: cannot unset: readonly variable\n", name);
          exit_status = 1;
        } else {
          unset_variable(variable_table, name);
          success = true;
        }
      }
    }

    if (!success) {
      printf("unset: \"%s\" not found\n", name);
    }
  }

  return exit_status;
}