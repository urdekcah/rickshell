#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>

int execute_command(Command* cmd);
int execute_command_list(CommandList* list);
int parse_and_execute(const char* input);
#endif /* __RICKSHELL_EXECUTE_H__ */