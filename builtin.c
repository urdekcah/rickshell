#include <string.h>
#include "builtin.h"
#include "job.h"

static builtin_command builtin_commands[] = {
  {"cd", builtin_cd},
  {"echo", builtin_echo},
  {"readonly", builtin_readonly},
  {NULL, NULL}
};

int execute_builtin(Command* cmd) {
  builtin_func func = get_builtin_func(cmd->argv.data[0]);
  if (func) {
    return func(cmd);
  }
  return -1;
}

builtin_func get_builtin_func(const char* name) {
  for (int i = 0; builtin_commands[i].name != NULL; i++) {
    if (strcmp(name, builtin_commands[i].name) == 0) {
      return builtin_commands[i].func;
    }
  }
  return NULL;
}