/*
 * parser.h — Recursive-descent parser from source text to a syntax tree.
 *
 * The grammar handled here is:
 *
 *   list      := and_or ( ( ';' | '&' | '\n' ) and_or )* sep?
 *   and_or    := pipeline ( ( '&&' | '||' ) linebreak pipeline )*
 *   pipeline  := [ '!' ] command ( '|' linebreak command )*
 *   command   := '(' list ')' redirect*
 *              | '{' list '}' redirect*
 *              | 'if' list 'then' list ( 'elif' list 'then' list )*
 *                    ( 'else' list )? 'fi'
 *              | ( 'while' | 'until' ) list 'do' list 'done'
 *              | 'for' NAME ( 'in' WORD* )? sep? 'do' list 'done'
 *              | 'for' '((' expr ';' expr ';' expr '))' sep? 'do' list 'done'
 *              | 'case' WORD 'in' ( pattern_list ')' list case_term )* 'esac'
 *              | 'select' NAME ( 'in' WORD* )? sep? 'do' list 'done'
 *              | '((' expr '))'
 *              | '[[' cond_expr ']]'
 *              | ( NAME '(' ')' | 'function' NAME ( '(' ')' )? ) command
 *              | simple
 *   simple    := ( io_redirect | WORD )+
 *
 * Reserved words are recognized only in command position; elsewhere they are
 * ordinary words. The parser owns no global state; nested constructs (subshells,
 * command substitution bodies, function bodies) are handled by ordinary
 * recursion.
 */
#ifndef __RICKSHELL_PARSER_H__
#define __RICKSHELL_PARSER_H__
#include <stdbool.h>
#include "ast.h"
#include "rstring.h"

/**
 * @brief Parses a complete program into an owned syntax tree.
 *
 * @param[in]  src     NUL-terminated source. Must not be NULL.
 * @param[out] out     Receives the owned tree (a NODE_LIST) on success, or NULL
 *                     when the input contained no commands. Free with node_free().
 *                     Must not be NULL.
 * @param[out] errmsg  Set to an owned message on failure, or an empty literal on
 *                     success. Must not be NULL. Free with string__free() only
 *                     when the call returned false.
 *
 * @retval true   Parsed successfully; @p *out is set (possibly NULL for empty input).
 * @retval false  Lexical or syntax error; @p *errmsg describes it and @p *out is NULL.
 */
bool parse_program(const char* src, Node** out, string* errmsg);

/**
 * @brief Reports whether @p src is an incomplete command needing more input.
 *
 * Parses @p src and discards the result, returning true only when the parse
 * failed because the input ended in the middle of a construct (an unterminated
 * quote or expansion, or a compound command missing its closing keyword). An
 * interactive reader uses this to keep reading continuation lines rather than
 * reporting a premature syntax error. A complete program — whether it parses
 * cleanly or contains a genuine syntax error — returns false.
 *
 * @param[in] src  NUL-terminated source. Must not be NULL.
 *
 * @retval true   The input is unterminated; more input could complete it.
 * @retval false  The input is complete (valid or definitively in error).
 */
bool parse_input_incomplete(const char* src);

#endif /* __RICKSHELL_PARSER_H__ */
