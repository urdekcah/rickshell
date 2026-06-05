/*
 * function.h — Registry of shell function definitions.
 *
 * A function definition binds a name to a compound-command body. The registry
 * owns a deep copy of each body so it survives after the input line that defined
 * it is freed; the executor walks the stored body on every call. The registry is
 * process-global and not thread-safe.
 */
#ifndef __RICKSHELL_FUNCTION_H__
#define __RICKSHELL_FUNCTION_H__
#include <stdbool.h>
#include "ast.h"
#include "rstring.h"

/**
 * @brief Resets the function registry to empty.
 *
 * Safe to call once at start-up. Calling it on a populated registry leaks the
 * existing entries; use cleanup_functions() to release them first.
 */
void init_functions(void);

/**
 * @brief Defines or redefines a function.
 *
 * Stores an owned deep copy of @p body under @p name. A prior definition of the
 * same name is released and replaced. Neither argument is retained: the caller
 * keeps ownership of @p name and @p body.
 *
 * @param[in] name  Function name. Must not be NULL or empty.
 * @param[in] body  Compound-command body to copy. Must not be NULL.
 */
void define_function(const string name, const Node* body);

/**
 * @brief Looks up a function body by name.
 *
 * @param[in] name  Function name. Must not be NULL.
 *
 * @return Borrowed pointer to the stored body, owned by the registry and valid
 *         until the function is redefined, removed, or the registry is cleaned
 *         up. NULL when no function of that name exists. The caller must not free
 *         the result.
 */
Node* find_function(const string name);

/**
 * @brief Removes a function definition.
 *
 * @param[in] name  Function name. Must not be NULL.
 *
 * @retval true   A definition was found and released.
 * @retval false  No function of that name existed.
 */
bool unset_function(const string name);

/**
 * @brief Releases every function definition and the registry's storage.
 */
void cleanup_functions(void);

#endif /* __RICKSHELL_FUNCTION_H__ */
