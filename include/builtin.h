#ifndef __RICKSHELL_BUILTIN_H__
#define __RICKSHELL_BUILTIN_H__
#include "expr.h"
#include "rstring.h"

typedef int (*builtin_func)(Command* cmd);
#define BUILTIN_FUNCS_SIZE 11

typedef struct {
  const string name;
  builtin_func func;
} builtin_command;

int builtin_cd(Command* cmd);
int builtin_declare(Command *cmd);
int builtin_echo(Command *cmd);
int builtin_exit(Command *cmd);
int builtin_export(Command *cmd);
int builtin_help(Command *cmd);
int builtin_history(Command *cmd);
int builtin_printf(Command *cmd);
int builtin_readonly(Command *cmd);
int builtin_set(Command *cmd);
int builtin_unset(Command *cmd);

int execute_builtin(Command* cmd);
builtin_func get_builtin_func(const string name);

typedef struct {
  const string name;
  const string short_desc;
  const string usage;
  const string long_desc;
} HelpInfo;
#endif /* __RICKSHELL_BUILTIN_H__ */