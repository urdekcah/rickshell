#ifndef __RICKSHELL_BUILTIN_H__
#define __RICKSHELL_BUILTIN_H__
#include "expr.h"

typedef int (*builtin_func)(Command* cmd);

typedef struct {
  const char* name;
  builtin_func func;
} builtin_command;

int builtin_cd(Command* cmd);
int builtin_echo(Command *cmd);
int builtin_readonly(Command *cmd);
int builtin_set(Command *cmd);
int builtin_unset(Command *cmd);

int execute_builtin(Command* cmd);
builtin_func get_builtin_func(const char* name);
#endif /* __RICKSHELL_BUILTIN_H__ */