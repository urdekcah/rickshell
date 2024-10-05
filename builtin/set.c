#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "variable.h"
#include "builtin.h"
#include "expr.h"
#include "error.h"

extern VariableTable* variable_table;

int builtin_set(Command* cmd) {
  if (cmd == NULL || cmd->argv.data == NULL) {
    return -1;
  }

  for (size_t i = 1; i < cmd->argv.size; i++) {
    char* arg = cmd->argv.data[i];
    char* equals_sign = strchr(arg, '=');
    if (equals_sign != NULL) {
      *equals_sign = '\0';
      const char* name = arg;
      const char* value = equals_sign + 1;

      if (*value == '\0' && i + 1 < cmd->argv.size) {
        value = cmd->argv.data[++i];
      }

      Variable* var = set_variable(variable_table, name, value, parse_variable_type(value), false);
      if (var == NULL) {
        print_error("Failed to set variable");
        return 0;
      }

      *equals_sign = '=';
    } else {
      Variable* var = get_variable(variable_table, arg);
      if (var) {
        printf("%s=%s\n", var->name, var->str);
      }
    }
  }

  return 0;
}