#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "string.h"
#include "array.h"
#include "result.h"
#include "ast.h"

/**
 * @brief Parses one input line and executes the resulting program.
 *
 * Lexes, parses, and walks the syntax tree. A syntax error is reported to the
 * user and reflected in @p *result; it is not returned as an error so the
 * interactive loop can continue.
 *
 * @param[in]  input   NUL-terminated source line. Must not be NULL or empty.
 * @param[out] result  Receives the exit status of the last command, or 2 on a
 *                     parse error. Must not be NULL.
 *
 * @return Ok on completion (including handled parse errors); an Err only for an
 *         internal execution failure such as fork().
 */
IntResult parse_and_execute(const string input, int* result);

/**
 * @brief Executes one syntax-tree node, updating the running exit status.
 *
 * @param[in]  node    Node to execute. NULL is a no-op. Ownership stays with
 *                     the caller.
 * @param[out] result  Receives the node's exit status. Must not be NULL.
 *
 * @return Ok on completion; an Err only for an internal execution failure.
 */
IntResult execute_node(Node* node, int* result);

/**
 * @brief Runs a command-substitution body and captures its standard output.
 *
 * Forks a child shell that parses and executes @p src with its stdout
 * redirected to a pipe; the parent collects the output. Trailing newlines are
 * the caller's concern; this returns the raw captured bytes.
 *
 * @param[in] src  NUL-terminated source to run. Must not be NULL.
 *
 * @return A newly allocated @c string the caller must free with string__free().
 *         Empty on a pipe or fork failure.
 */
string command_substitution(const string src);

/**
 * @brief Replaces the current process image with @p __file, like execvp(3).
 *
 * @param[in] __file  Command name or path. Must not be NULL.
 * @param[in] __argv  Argument vector as shell strings.
 *
 * @retval -1  exec failed; errno is set. Does not return on success.
 */
int rexecvp(const string __file, StringArray __argv);
#endif /* __RICKSHELL_EXECUTE_H__ */