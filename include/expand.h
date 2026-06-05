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
#ifndef __RICKSHELL_EXPAND_H__
#define __RICKSHELL_EXPAND_H__
#include "word.h"
#include "array.h"
#include "rstring.h"
#include "variable.h"

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

/**
 * @brief Evaluates an arithmetic expression to a signed 64-bit integer.
 *
 * Performs parameter expansion on @p expr, then evaluates it with the same
 * precedence-climbing engine used by "$(( ))". A syntax error or division by
 * zero yields false and writes 0.
 *
 * @param[in]  table  Variable table for identifier lookups. Must not be NULL.
 * @param[in]  expr   Expression text. Must not be NULL.
 * @param[out] out    Receives the result, or 0 on error. Must not be NULL.
 *
 * @retval true   The expression evaluated successfully.
 * @retval false  The expression was malformed or divided by zero; @p *out is 0.
 */
bool expand_arith(VariableTable* table, const string expr, long long* out);

/**
 * @brief Matches a string against an unexpanded shell pattern word.
 *
 * Expands @p pattern (parameter, tilde, command, and arithmetic expansion) and
 * matches it against @p subject using POSIX shell pattern rules ('*', '?', and
 * bracket expressions). Pattern metacharacters that were quoted in @p pattern
 * match literally; only unquoted ones act as wildcards. This is the matcher used
 * by case clauses and by the "==" / "!=" operators inside "[[ ]]".
 *
 * @param[in] table    Variable table. Must not be NULL.
 * @param[in] subject  String to test. Must not be NULL.
 * @param[in] pattern  Pattern word. Must not be NULL.
 *
 * @return True when @p subject matches @p pattern.
 */
bool expand_pattern_match(VariableTable* table, const string subject, const Word* pattern);

/**
 * @brief Matches a string against a POSIX extended regular expression.
 *
 * Used by the "=~" operator inside "[[ ]]". The caller is responsible for any
 * parameter expansion of @p regex before calling.
 *
 * @param[in] subject  String to test. Must not be NULL.
 * @param[in] regex    Extended regular expression. Must not be NULL.
 *
 * @return True when @p subject matches @p regex. A regex that fails to compile
 *         yields false.
 */
bool expand_regex_match(const string subject, const string regex);

#endif /* __RICKSHELL_EXPAND_H__ */
