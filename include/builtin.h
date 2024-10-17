#ifndef __RICKSHELL_BUILTIN_H__
#define __RICKSHELL_BUILTIN_H__
#include "expr.h"
#include "rstring.h"

typedef int (*builtin_func)(Command* cmd);

typedef struct {
  const string name;
  builtin_func func;
} builtin_command;

int builtin_cd(Command* cmd);
int builtin_declare(Command *cmd);
int builtin_echo(Command *cmd);
int builtin_export(Command *cmd);
int builtin_readonly(Command *cmd);
int builtin_set(Command *cmd);
int builtin_unset(Command *cmd);

int execute_builtin(Command* cmd);
builtin_func get_builtin_func(const string name);
#endif /* __RICKSHELL_BUILTIN_H__ */