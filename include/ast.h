#ifndef __RICKSHELL_AST_H__
#define __RICKSHELL_AST_H__
#include <stdbool.h>
#include <stddef.h>
#include "lexer.h"
#include "word.h"

/*
 * ast.h — Syntax tree produced by the parser and walked by the executor.
 *
 * The tree mirrors the grammar directly: a list holds and-or chains, an and-or
 * chain holds pipelines, a pipeline holds commands, and a command is either a
 * simple command, a parenthesized subshell, or a brace group. Words are kept
 * unexpanded; the executor expands them only when it reaches the command, which
 * is what lets a single subshell or command substitution be re-entered cleanly.
 */

/**
 * @brief Tag for the active member of @c Node.
 */
typedef enum {
  NODE_LIST,      /**< A sequence of and-or chains separated by ';', '&', or newline. */
  NODE_AND_OR,    /**< Pipelines joined by "&&" / "||", evaluated left to right. */
  NODE_PIPELINE,  /**< Commands joined by "|". */
  NODE_SIMPLE,    /**< A simple command: words and redirections. */
  NODE_SUBSHELL,  /**< "( list )": body runs in a forked child. */
  NODE_GROUP,     /**< "{ list; }": body runs in the current shell. */
} NodeType;

/**
 * @brief A single redirection attached to a command, group, or subshell.
 *
 * The target is left unexpanded until execution. @c fd is the explicit
 * left-hand descriptor (as in "2>file"); when no descriptor was written it is
 * -1 and the executor substitutes the operator's default (0 for input, 1 for
 * output).
 */
typedef struct {
  TokenType op;      /**< Redirection operator token (e.g. TOK_GREAT, TOK_DGREAT). */
  int       fd;      /**< Explicit left-hand fd, or -1 for the operator default. */
  Word*     target;  /**< Owned target word (filename or fd reference). */
} Redir;

typedef struct Node Node;

/**
 * @brief One syntax-tree node. The active union member is selected by @c type.
 *
 * Ownership: every node owns its child nodes, word arrays, and redirection
 * targets. Release an entire tree with node_free().
 */
struct Node {
  NodeType type;  /**< Selects the active union member. */
  union {
    /** NODE_LIST: @c items[i] is followed by separator @c seps[i] (';' or '&'). */
    struct {
      Node** items;
      char*  seps;
      size_t n;
    } list;
    /** NODE_AND_OR: @c ops[i] is the connector before @c items[i] ('a' for "&&",
     *  'o' for "||"); @c ops[0] is unused. */
    struct {
      Node** items;
      char*  ops;
      size_t n;
    } and_or;
    /** NODE_PIPELINE: @c cmds run with their stdout/stdin chained; @c bang
     *  negates the final exit status. */
    struct {
      Node** cmds;
      size_t n;
      bool   bang;
    } pipeline;
    /** NODE_SIMPLE: argument @c words plus @c redirs, both pre-expansion. */
    struct {
      Word**  words;
      size_t  nwords;
      Redir*  redirs;
      size_t  nredirs;
    } simple;
    /** NODE_SUBSHELL and NODE_GROUP: a @c body list with optional @c redirs
     *  applied to the whole compound command. */
    struct {
      Node*  body;
      Redir* redirs;
      size_t nredirs;
    } group;
  } u;
};

/**
 * @brief Allocates a node of the given type with empty/zeroed contents.
 *
 * @param[in] type  Node kind.
 * @return Owned node; never NULL. Free with node_free().
 */
Node* node_new(NodeType type);

/**
 * @brief Recursively frees a node and everything it owns.
 *
 * @param[in] n  Node to free. NULL is a no-op.
 */
void node_free(Node* n);

/**
 * @brief Appends an and-or chain to a list with its trailing separator.
 *
 * @param[in,out] list  A NODE_LIST. Must not be NULL.
 * @param[in]     item  Child node; ownership transfers to @p list.
 * @param[in]     sep   ';' for sequential, '&' for background.
 */
void node_list_push(Node* list, Node* item, char sep);

/**
 * @brief Appends a pipeline to an and-or chain with its leading connector.
 *
 * @param[in,out] andor     A NODE_AND_OR. Must not be NULL.
 * @param[in]     pipeline  Child node; ownership transfers to @p andor.
 * @param[in]     op        'a' for "&&", 'o' for "||"; ignored for the first element.
 */
void node_and_or_push(Node* andor, Node* pipeline, char op);

/**
 * @brief Appends a command to a pipeline.
 *
 * @param[in,out] pipeline  A NODE_PIPELINE. Must not be NULL.
 * @param[in]     cmd       Child node; ownership transfers to @p pipeline.
 */
void node_pipeline_push(Node* pipeline, Node* cmd);

/**
 * @brief Appends an argument word to a simple command.
 *
 * @param[in,out] simple  A NODE_SIMPLE. Must not be NULL.
 * @param[in]     word    Word; ownership transfers to @p simple.
 */
void node_simple_push_word(Node* simple, Word* word);

/**
 * @brief Appends a redirection to a simple command.
 *
 * @param[in,out] simple  A NODE_SIMPLE. Must not be NULL.
 * @param[in]     op      Redirection operator token.
 * @param[in]     fd      Explicit left-hand fd, or -1 for the operator default.
 * @param[in]     target  Target word; ownership transfers to @p simple.
 */
void node_simple_push_redir(Node* simple, TokenType op, int fd, Word* target);

/**
 * @brief Appends a redirection to a subshell or brace group.
 *
 * @param[in,out] group   A NODE_SUBSHELL or NODE_GROUP. Must not be NULL.
 * @param[in]     op      Redirection operator token.
 * @param[in]     fd      Explicit left-hand fd, or -1 for the operator default.
 * @param[in]     target  Target word; ownership transfers to @p group.
 */
void node_group_push_redir(Node* group, TokenType op, int fd, Word* target);

#endif /* __RICKSHELL_AST_H__ */
