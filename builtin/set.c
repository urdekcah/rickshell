#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "variable.h"
#include "builtin.h"
#include "expr.h"
#include "error.h"
#include "rstring.h"
#include "array.h"
#include "strconv.h"
#include "io.h"

extern VariableTable* variable_table;

int builtin_set(Command* cmd) {
  if (cmd == NULL || cmd->argv.size == 0) {
    return -1;
  }

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_checked_get(cmd->argv, i);
    ssize_t equals_pos = string__indexof(elem, _SLIT("="));
    if (equals_pos != -1) {
      string name = string__substring(elem, 0, equals_pos);
      string value = string__substring(elem, equals_pos + 1);
      Variable* var = set_variable(variable_table, name, value, parse_variable_type(value), false);
      if (var == NULL) {
        return 0;
      }
      string__free(name);
      string__free(value);
    } else {
      Variable* var = get_variable(variable_table, elem);
      if (var) {
        string value = va_value_to_string(&var->value);
        fprintln("%S=%S", var->name, value);
        string__free(value);
      }
    }
  }
  return 0;
}