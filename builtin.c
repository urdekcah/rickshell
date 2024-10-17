#include <string.h>
#include "builtin.h"
#include "job.h"
#include "rstring.h"
#include "array.h"

static builtin_command builtin_commands[] = {
  {_SLIT("cd"), builtin_cd},
  {_SLIT("declare"), builtin_declare},
  {_SLIT("echo"), builtin_echo},
  {_SLIT("exit"), builtin_exit},
  {_SLIT("export"), builtin_export},
  {_SLIT("printf"), builtin_printf},
  {_SLIT("readonly"), builtin_readonly},
  {_SLIT("set"), builtin_set},
  {_SLIT("unset"), builtin_unset},
  {_SLIT0, NULL}
};

int execute_builtin(Command* cmd) {
  builtin_func func = get_builtin_func(*(string*)array_checked_get(cmd->argv, 0));
  if (func) {
    return func(cmd);
  }
  return -1;
}

builtin_func get_builtin_func(const string name) {
  register size_t i;
  for (i = 0; !string__is_null_or_empty(builtin_commands[i].name); i++) {
    if (string__compare(name, builtin_commands[i].name) == 0) {
      return builtin_commands[i].func;
    }
  }
  return NULL;
}