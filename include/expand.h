#ifndef __RICKSHELL_EXPAND_H__
#define __RICKSHELL_EXPAND_H__
#include "word.h"
#include "array.h"
#include "rstring.h"
#include "variable.h"

/*
 * expand.h — Word expansion pass.
 *
 * Turns a pre-expansion Word (a list of quote-tagged segments) into the actual
 * argument strings a command receives, applying the POSIX stages in order:
 * tilde, parameter, command substitution, arithmetic, field splitting,
 * pathname expansion (globbing), and quote removal. Quote removal is implicit:
 * the lexer never stored quote characters, so nothing here has to strip them.
 *
 * Splitting and globbing apply only to the results of unquoted expansions; the
 * Word segments carry the quoting context that decides this per piece.
 */

/**
 * @brief Expands a word into zero or more fields.
 *
 * Applies every expansion stage including field splitting and globbing. An
 * unquoted expansion of an unset or empty variable can yield zero fields; an
 * empty quoted string yields exactly one empty field.
 *
 * @param[in] table  Variable table for parameter and arithmetic lookups. Must
 *                   not be NULL.
 * @param[in] w      Word to expand. Must not be NULL.
 *
 * @return A StringArray of owned @c string fields. The caller owns the array
 *         and every field; free each field with string__free() and the array
 *         with array_free().
 */
StringArray expand_word_to_fields(VariableTable* table, const Word* w);

/**
 * @brief Expands a word without field splitting or globbing, joined to one string.
 *
 * Used where a single value is required regardless of IFS or pattern characters,
 * such as a redirection target or an assignment right-hand side.
 *
 * @param[in] table  Variable table. Must not be NULL.
 * @param[in] w      Word to expand. Must not be NULL.
 *
 * @return A newly allocated @c string the caller must free with string__free().
 */
string expand_word_to_string(VariableTable* table, const Word* w);

#endif /* __RICKSHELL_EXPAND_H__ */
