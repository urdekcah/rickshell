#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "string.h"
#include "array.h"
#include "result.h"

/**
 * @param[in]  cmd
 * @param[out] result
 */
IntResult execute_command(Command* cmd, int* result);
/**
 * @param[in]  list
 * @param[out] result
 */
IntResult execute_command_list(CommandList* list, int* result);
/**
 * @param[in]  input
 * @param[out] result
 */
IntResult parse_and_execute(const string input, int* result);
string command_to_string(Command* cmd);
int rexecvp(const string __file, StringArray __argv);
#endif /* __RICKSHELL_EXECUTE_H__ */