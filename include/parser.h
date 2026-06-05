#ifndef __RICKSHELL_PARSER_H__
#define __RICKSHELL_PARSER_H__
#include <stdbool.h>
#include "ast.h"
#include "rstring.h"

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
 *              | simple
 *   simple    := ( io_redirect | WORD )+
 *
 * The parser owns no global state; nested constructs (subshells, command
 * substitution bodies) are handled by ordinary recursion.
 */

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

#endif /* __RICKSHELL_PARSER_H__ */
