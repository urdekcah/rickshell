/*
 * param.h — Positional parameters ($1, $2, ..., $@, $*, $#).
 *
 * The parameters are held in one process-global, ordered list. A function call
 * swaps in its own list for the duration of the call and restores the caller's
 * afterwards, which is what gives a function its own $1..$N and $#. The list is
 * not thread-safe.
 */
#ifndef __RICKSHELL_PARAM_H__
#define __RICKSHELL_PARAM_H__
#include <stddef.h>
#include "array.h"
#include "rstring.h"

/**
 * @brief Resets the positional-parameter list to empty.
 *
 * Safe to call once at start-up. On an already-populated list this leaks the
 * existing entries; call params_cleanup() first to release them.
 */
void params_init(void);

/**
 * @brief Releases the positional-parameter list and its storage.
 */
void params_cleanup(void);

/**
 * @brief Returns the number of positional parameters (the value of $#).
 *
 * @return Current parameter count.
 */
size_t params_count(void);

/**
 * @brief Returns a copy of the @p index-th positional parameter.
 *
 * @param[in] index  One-based parameter index ($1 is index 1).
 *
 * @return A newly allocated @c string the caller must free with string__free();
 *         an empty string when @p index is 0 or out of range.
 */
string params_get(size_t index);

/**
 * @brief Joins all positional parameters into one string with @p sep between them.
 *
 * Models the unquoted expansion of "$*" and "$@".
 *
 * @param[in] sep  Separator placed between parameters.
 *
 * @return A newly allocated @c string the caller must free with string__free().
 */
string params_join(char sep);

/**
 * @brief Returns a deep copy of the entire positional-parameter list.
 *
 * @return A StringArray of owned @c string entries. The caller owns the array
 *         and every entry; free each with string__free() and the array with
 *         array_free(). Used both to snapshot for save/restore and to iterate
 *         the parameters (as in "for name; do ...").
 */
StringArray params_snapshot(void);

/**
 * @brief Replaces the positional-parameter list, taking ownership of @p params.
 *
 * The previous list and its entries are released. @p params and its entries are
 * adopted as-is; the caller must not free them afterwards.
 *
 * @param[in] params  StringArray of owned @c string entries to adopt.
 */
void params_replace(StringArray params);

#endif /* __RICKSHELL_PARAM_H__ */
