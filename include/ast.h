/*
 * ast.h — Syntax tree produced by the parser and walked by the executor.
 *
 * The tree mirrors the grammar directly: a list holds and-or chains, an and-or
 * chain holds pipelines, a pipeline holds commands, and a command is either a
 * simple command, a parenthesized subshell, a brace group, or one of the
 * compound commands (if, for, while/until, case, select, the arithmetic and
 * conditional commands, or a function definition). Words are kept unexpanded;
 * the executor expands them only when it reaches the command, which is what lets
 * a loop body be parsed once and run repeatedly, and a command substitution be
 * re-entered cleanly.
 */
#ifndef __RICKSHELL_AST_H__
#define __RICKSHELL_AST_H__
#include <stdbool.h>
#include <stddef.h>
#include "lexer.h"
#include "word.h"

/**
 * @brief Tag for the active member of @c Node.
 */
typedef enum {
  NODE_LIST,       /**< A sequence of and-or chains separated by ';', '&', or newline. */
  NODE_AND_OR,     /**< Pipelines joined by "&&" / "||", evaluated left to right. */
  NODE_PIPELINE,   /**< Commands joined by "|". */
  NODE_SIMPLE,     /**< A simple command: words and redirections. */
  NODE_SUBSHELL,   /**< "( list )": body runs in a forked child. */
  NODE_GROUP,      /**< "{ list; }": body runs in the current shell. */
  NODE_IF,         /**< "if list; then list; [elif ...] [else list] fi". */
  NODE_FOR,        /**< "for name [in words]; do list; done". */
  NODE_FOR_ARITH,  /**< "for ((init; cond; update)); do list; done". */
  NODE_WHILE,      /**< "while list; do list; done", or "until ..." when @c until is set. */
  NODE_CASE,       /**< "case word in pattern) list ;; ... esac". */
  NODE_SELECT,     /**< "select name [in words]; do list; done". */
  NODE_ARITH,      /**< "(( expression ))": evaluate and set exit status from the result. */
  NODE_COND,       /**< "[[ expression ]]": the conditional expression command. */
  NODE_FUNCDEF,    /**< A function definition: name plus a compound-command body. */
  NODE_REDIR,      /**< A compound command with trailing redirections applied around it. */
} NodeType;

typedef struct Node Node;

/**
 * @brief A single redirection attached to a command, group, or compound command.
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

/**
 * @brief Kind of a node in a "[[ ]]" conditional expression.
 */
typedef enum {
  COND_STRING,  /**< A bare word; true when its expansion is non-empty. */
  COND_UNARY,   /**< A unary operator applied to one operand (e.g. "-f file"). */
  COND_BINARY,  /**< A binary operator between two operands (e.g. "a == b"). */
  COND_NOT,     /**< Logical negation of @c a. */
  COND_AND,     /**< Logical "&&" of @c a and @c b. */
  COND_OR,      /**< Logical "||" of @c a and @c b. */
} CondType;

typedef struct CondNode CondNode;

/**
 * @brief One node of a "[[ ]]" conditional-expression tree.
 *
 * Ownership: a node owns its operand words (@c left, @c right) and its child
 * nodes (@c a, @c b). Only the members relevant to @c type are used; the rest
 * are NULL. Release a whole tree with cond_free().
 */
struct CondNode {
  CondType  type;   /**< Selects which members below are active. */
  string    op;     /**< Operator text for COND_UNARY / COND_BINARY (owned), else empty. */
  Word*     left;   /**< Left/sole operand for COND_STRING / COND_UNARY / COND_BINARY. */
  Word*     right;  /**< Right operand for COND_BINARY. */
  CondNode* a;      /**< First child for COND_NOT / COND_AND / COND_OR. */
  CondNode* b;      /**< Second child for COND_AND / COND_OR. */
};

/**
 * @brief One "pattern) list" clause of a case statement.
 *
 * Ownership: the item owns its @c patterns array, every pattern word, and its
 * @c body node.
 */
typedef struct {
  Word**  patterns;   /**< Alternative patterns ("a|b|c"); matched after expansion. */
  size_t  npatterns;  /**< Number of entries in @c patterns. */
  Node*   body;       /**< Clause body; NULL when the clause is empty. */
  int     terminator; /**< 0 for ";;", 1 for ";&" (fall through), 2 for ";;&" (resume testing). */
} CaseItem;

/**
 * @brief One syntax-tree node. The active union member is selected by @c type.
 *
 * Ownership: every node owns its child nodes, word arrays, redirection targets,
 * and any owned strings. Release an entire tree with node_free().
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
    /** NODE_SUBSHELL, NODE_GROUP, and NODE_REDIR: a @c body with optional
     *  @c redirs applied to the whole construct. For NODE_REDIR the body is the
     *  wrapped compound command and @c redirs is always non-empty. */
    struct {
      Node*  body;
      Redir* redirs;
      size_t nredirs;
    } group;
    /** NODE_IF: run @c then_body when @c cond succeeds, else @c else_part.
     *  @c else_part is NULL, another NODE_IF (an "elif"), or an "else" body. */
    struct {
      Node* cond;
      Node* then_body;
      Node* else_part;
    } if_clause;
    /** NODE_FOR and NODE_SELECT: bind @c name to each field of the expanded
     *  @c words and run @c body. When @c have_in is false the word list was
     *  omitted and the loop iterates the positional parameters. */
    struct {
      string name;
      Word** words;
      size_t nwords;
      bool   have_in;
      Node*  body;
    } for_loop;
    /** NODE_FOR_ARITH: C-style loop; each clause is an arithmetic expression
     *  text (empty means absent — an absent condition is treated as true). */
    struct {
      string init;
      string cond;
      string update;
      Node*  body;
    } for_arith;
    /** NODE_WHILE: repeat @c body while @c cond succeeds; @c until inverts the
     *  test so the loop runs while @c cond fails. */
    struct {
      Node* cond;
      Node* body;
      bool  until;
    } while_loop;
    /** NODE_CASE: match the expansion of @c subject against each clause. */
    struct {
      Word*     subject;
      CaseItem* items;
      size_t    nitems;
    } case_stmt;
    /** NODE_ARITH: evaluate @c expr; exit status is 0 when the result is
     *  non-zero, 1 otherwise. */
    struct {
      string expr;
    } arith;
    /** NODE_COND: the "[[ ]]" conditional-expression tree. */
    struct {
      CondNode* expr;
    } cond;
    /** NODE_FUNCDEF: bind @c name to the compound-command @c body. */
    struct {
      string name;
      Node*  body;
    } funcdef;
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
 * @brief Deep-copies a node and everything it owns.
 *
 * The clone shares no memory with the original, so the original may be freed
 * independently. This is what lets a function body outlive the input line that
 * defined it: the definition clones the body into the function table.
 *
 * @param[in] n  Node to copy. NULL yields NULL.
 * @return Owned deep copy. Free with node_free().
 */
Node* node_clone(const Node* n);

/**
 * @brief Allocates a conditional-expression node of the given kind.
 *
 * @param[in] type  Conditional node kind.
 * @return Owned node with zeroed members; never NULL. Free with cond_free().
 */
CondNode* cond_new(CondType type);

/**
 * @brief Recursively frees a conditional-expression tree.
 *
 * @param[in] c  Tree to free. NULL is a no-op.
 */
void cond_free(CondNode* c);

/**
 * @brief Deep-copies a conditional-expression tree.
 *
 * @param[in] c  Tree to copy. NULL yields NULL.
 * @return Owned deep copy. Free with cond_free().
 */
CondNode* cond_clone(const CondNode* c);

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

/**
 * @brief Appends a word to a for-loop or select word list.
 *
 * @param[in,out] loop  A NODE_FOR or NODE_SELECT. Must not be NULL.
 * @param[in]     word  Word; ownership transfers to @p loop.
 */
void node_for_push_word(Node* loop, Word* word);

/**
 * @brief Appends a blank clause to a case statement and returns it for filling.
 *
 * @param[in,out] casenode  A NODE_CASE. Must not be NULL.
 * @return Pointer to the newly appended, zeroed clause. Valid only until the
 *         next node_case_push_item() call on the same node (which may move the
 *         clause array); fill it before appending another clause.
 */
CaseItem* node_case_push_item(Node* casenode);

/**
 * @brief Appends a pattern word to a case clause.
 *
 * @param[in,out] item  Clause obtained from node_case_push_item(). Must not be NULL.
 * @param[in]     pat   Pattern word; ownership transfers to @p item.
 */
void case_item_push_pattern(CaseItem* item, Word* pat);

#endif /* __RICKSHELL_AST_H__ */
