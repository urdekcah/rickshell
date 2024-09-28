#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "builtin.h"
#include "variable.h"
#include "error.h"
#include "expr.h"

extern VariableTable* variable_table;

int builtin_readonly(Command *cmd) {
  bool display_all = false;
  bool refer_to_functions = false;
  bool refer_to_indexed_array = false;
  bool refer_to_associative_array = false;
  int option_end = 1;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    if (cmd->argv.data[i][0] == '-' && cmd->argv.data[i][1] != '-') {
      for (size_t j = 1; cmd->argv.data[i][j] != '\0'; j++) {
        switch (cmd->argv.data[i][j]) {
          case 'p':
            display_all = true;
            break;
          case 'f':
            refer_to_functions = true;
            break;
          case 'a':
            refer_to_indexed_array = true;
            break;
          case 'A':
            refer_to_associative_array = true;
            break;
          default:
            print_error("Invalid option");
            return 1;
        }
      }
      option_end = i + 1;
    } else if (strcmp(cmd->argv.data[i], "--") == 0) {
      option_end = i + 1;
      break;
    } else {
      break;
    }
  }

  if (display_all) {
    for (int i = 0; i < variable_table->size; i++) {
      Variable *var = &variable_table->variables[i];
      if (var->readonly) {
        if ((refer_to_functions && var->is_function) ||
          (!refer_to_functions && !var->is_function)) {
          printf("readonly %s", var->name);
          if (var->value) {
            printf("=\"%s\"", var->value);
          }
          printf("\n");
        }
      }
    }
    return 0;
  }

  for (size_t i = option_end; i < cmd->argv.size; i++) {
    char *arg = cmd->argv.data[i];
    char *equals = strchr(arg, '=');
    char *name, *value = NULL;
    
    if (equals) {
      *equals = '\0';
      name = arg;
      value = equals + 1;
    } else {
      name = arg;
    }
    
    Variable *var = get_variable(variable_table, name);
    if (var) {
      if (value) {
        if (var->readonly) {
          print_error("Cannot modify readonly variable");
          return 1;
        }
        rfree(var->value);
        var->value = rstrdup(value);
      }
      var->readonly = true;
    } else {
      var = set_variable(variable_table, name, value ? value : "", true);
      if (var == NULL) {
        print_error("Failed to set readonly variable");
        return 1;
      }
    }
    
    if (refer_to_indexed_array) {
      var->is_indexed_array = true;
    } else if (refer_to_associative_array) {
      var->is_associative_array = true;
    } else if (refer_to_functions) {
      var->is_function = true;
    }
    
    if (equals) {
      *equals = '=';
    }
  }

  return 0;
}