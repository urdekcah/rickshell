#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "string.h"
#include "array.h"

int execute_command(Command* cmd);
int execute_command_list(CommandList* list);
int parse_and_execute(const string input);
string command_to_string(Command* cmd);
int rexecvp(const string __file, StringArray __argv);
#endif /* __RICKSHELL_EXECUTE_H__ */