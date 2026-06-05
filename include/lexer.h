#ifndef __RICKSHELL_LEXER_H__
#define __RICKSHELL_LEXER_H__
#include <stdbool.h>
#include <stddef.h>
#include "word.h"
#include "rstring.h"

/*
 * lexer.h — Hand-written, mode-based tokenizer for the shell grammar.
 *
 * The lexer is pull-driven: the parser calls lexer_next() to obtain one token
 * at a time. It holds no global state — all position and error state lives in
 * the caller-provided Lexer struct — so it is reentrant and a command
 * substitution can run its own nested lexer over a captured source string.
 *
 * Quoting is resolved here, not deferred and not destroyed: a TOK_WORD carries
 * a Word whose segments record whether each piece was single-quoted,
 * double-quoted, or unquoted, which the expansion pass needs to apply field
 * splitting and globbing correctly.
 */

/**
 * @brief Token kinds produced by the lexer.
 *
 * Operator spellings are noted per value. TOK_DLESS and TOK_DLESSDASH are
 * recognized so that here-documents tokenize cleanly; the parser reports them
 * as unsupported rather than mis-parsing them.
 */
typedef enum {
  TOK_EOF = 0,    /**< End of input. */
  TOK_NEWLINE,    /**< Unquoted newline; terminates a command. */
  TOK_WORD,       /**< A word; payload is @c Token.word. */
  TOK_IO_NUMBER,  /**< Digit run bound to a following redirection; payload is @c Token.io_number. */
  TOK_PIPE,       /**< "|" */
  TOK_OR_IF,      /**< "||" */
  TOK_AND_IF,     /**< "&&" */
  TOK_SEMI,       /**< ";" */
  TOK_AMP,        /**< "&" */
  TOK_LPAREN,     /**< "(" */
  TOK_RPAREN,     /**< ")" */
  TOK_LESS,       /**< "<" */
  TOK_GREAT,      /**< ">" */
  TOK_DGREAT,     /**< ">>" */
  TOK_DLESS,      /**< "<<" (here-document; parser-deferred) */
  TOK_DLESSDASH,  /**< "<<-" (here-document; parser-deferred) */
  TOK_LESSAND,    /**< "<&" */
  TOK_GREATAND,   /**< ">&" */
  TOK_LESSGREAT,  /**< "<>" */
  TOK_CLOBBER,    /**< ">|" */
  TOK_DGREATAND,  /**< ">>&" */
} TokenType;

/**
 * @brief A single lexed token.
 *
 * Ownership: when @c type is TOK_WORD, @c word is heap-owned and the caller
 * must release it (via word_free(), or token_free()). For all other kinds
 * @c word is NULL.
 */
typedef struct {
  TokenType type;       /**< Token kind. */
  Word*     word;       /**< Word payload when @c type is TOK_WORD, else NULL. */
  int       io_number;  /**< File descriptor when @c type is TOK_IO_NUMBER, else 0. */
} Token;

/**
 * @brief Lexer scan state over one borrowed source string.
 *
 * The lexer does not own @c src; the caller must keep it alive for the lexer's
 * lifetime. @c errmsg is owned by the lexer once @c error becomes true.
 */
typedef struct {
  const char* src;     /**< Borrowed, NUL-terminated source. Not freed by the lexer. */
  size_t      len;     /**< Byte length of @c src. */
  size_t      pos;     /**< Current scan offset into @c src. */
  bool        error;   /**< True once a lexical error has been recorded. */
  string      errmsg;  /**< Owned error description when @c error is true, else an empty literal. */
} Lexer;

/**
 * @brief Initializes a lexer over a borrowed source string.
 *
 * @param[out] lx   Lexer to initialize. Must not be NULL.
 * @param[in]  src  NUL-terminated source. Must not be NULL and must outlive @p lx.
 */
void lexer_init(Lexer* lx, const char* src);

/**
 * @brief Releases resources owned by a lexer.
 *
 * Frees @c errmsg if one was set. Does not free @c src. Safe to call once after
 * lexing completes.
 *
 * @param[in,out] lx  Lexer to dispose. Must not be NULL.
 */
void lexer_dispose(Lexer* lx);

/**
 * @brief Pulls the next token from the input.
 *
 * On success @p out is filled. When @p out->type is TOK_WORD, @p out->word is a
 * newly allocated word owned by the caller. At end of input the call still
 * succeeds and yields a TOK_EOF token. Repeated calls after EOF keep returning
 * TOK_EOF.
 *
 * @param[in,out] lx   Lexer state. Must not be NULL.
 * @param[out]    out  Receives the token. Must not be NULL. Left with type
 *                     TOK_EOF and no owned payload on error.
 *
 * @retval true   A token was produced (possibly TOK_EOF).
 * @retval false  A lexical error occurred; @p lx->error is set and
 *                @p lx->errmsg describes it. No token payload is owned by the caller.
 */
bool lexer_next(Lexer* lx, Token* out);

/**
 * @brief Frees any heap payload owned by a token.
 *
 * @param[in,out] t  Token to clear. Must not be NULL. Frees @c t->word when
 *                   present and resets @c t to a TOK_EOF token.
 */
void token_free(Token* t);

#endif /* __RICKSHELL_LEXER_H__ */
