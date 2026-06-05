/*
 * parser.c — Recursive-descent parser over the lexer's token stream.
 *
 * The parser keeps exactly one token of lookahead in Parser.tok and consumes it
 * with p_advance(). Word ownership moves into the tree via p_take_word(), which
 * detaches the word from the lookahead so the subsequent advance does not free
 * it; any word the parser steps over without taking (a reserved "{", "}" or "!")
 * is released by p_advance(). On the first error the parser records a message
 * and unwinds, freeing every partially built node on the way out.
 */
#include <stdbool.h>
#include <stddef.h>
#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "word.h"
#include "rstring.h"

typedef enum {
  STOP_EOF,     /**< Top-level list: ends at end of input. */
  STOP_RPAREN,  /**< Subshell body: ends at ')'. */
  STOP_RBRACE,  /**< Brace group body: ends at the reserved word '}'. */
} ListStop;

typedef struct {
  Lexer  lex;
  Token  tok;     /* current lookahead */
  bool   failed;
  string errmsg;  /* owned once failed is true */
} Parser;

static Node* parse_list(Parser* P, ListStop stop);

static void p_fill(Parser* P) {
  if (!lexer_next(&P->lex, &P->tok)) {
    P->failed = true;
    if (P->errmsg.len == 0) P->errmsg = string__from(P->lex.errmsg);
  }
}

static void p_advance(Parser* P) {
  if (P->tok.type == TOK_WORD && P->tok.word != NULL) {
    word_free(P->tok.word);
    P->tok.word = NULL;
  }
  p_fill(P);
}

static void p_error(Parser* P, const char* msg) {
  if (!P->failed) {
    P->failed = true;
    P->errmsg = string__new(msg);
  }
}

static TokenType cur(const Parser* P) {
  return P->tok.type;
}

/* Detaches the lookahead word and advances. The caller owns the returned word. */
static Word* p_take_word(Parser* P) {
  Word* w = P->tok.word;
  P->tok.word = NULL;
  p_advance(P);
  return w;
}

static void skip_newlines(Parser* P) {
  while (!P->failed && cur(P) == TOK_NEWLINE) p_advance(P);
}

/* True when the lookahead is an unquoted single-character literal word @p ch,
 * the form in which "{", "}" and "!" reach the parser as reserved words. */
static bool tok_is_reserved(const Token* t, char ch) {
  if (t->type != TOK_WORD || t->word == NULL || t->word->size != 1) return false;
  const WordSeg* s = &t->word->data[0];
  return s->kind == SEG_LITERAL && !s->quoted &&
         s->text.len == 1 && s->text.str[0] == ch;
}

static bool is_redir_token(TokenType t) {
  switch (t) {
    case TOK_LESS:
    case TOK_GREAT:
    case TOK_DGREAT:
    case TOK_DLESS:
    case TOK_DLESSDASH:
    case TOK_LESSAND:
    case TOK_GREATAND:
    case TOK_LESSGREAT:
    case TOK_CLOBBER:
    case TOK_DGREATAND:
      return true;
    default:
      return false;
  }
}

static bool at_list_end(const Parser* P, ListStop stop) {
  if (cur(P) == TOK_EOF) return true;
  if (stop == STOP_RPAREN && cur(P) == TOK_RPAREN) return true;
  if (stop == STOP_RBRACE && tok_is_reserved(&P->tok, '}')) return true;
  return false;
}

/* Parses one redirection (optionally prefixed by an IO_NUMBER) into its parts. */
static bool parse_one_redir(Parser* P, TokenType* op_out, int* fd_out, Word** target_out) {
  int fd = -1;
  if (cur(P) == TOK_IO_NUMBER) {
    fd = P->tok.io_number;
    p_advance(P);
  }
  TokenType op = cur(P);
  if (!is_redir_token(op)) {
    p_error(P, "expected a redirection operator");
    return false;
  }
  if (op == TOK_DLESS || op == TOK_DLESSDASH) {
    p_error(P, "here-documents are not supported yet");
    return false;
  }
  p_advance(P);
  if (cur(P) != TOK_WORD) {
    p_error(P, "expected a redirection target");
    return false;
  }
  *target_out = p_take_word(P);
  *op_out = op;
  *fd_out = fd;
  return true;
}

/* Consumes redirections that trail a subshell or brace group. */
static void parse_group_redirs(Parser* P, Node* group) {
  while (!P->failed && (cur(P) == TOK_IO_NUMBER || is_redir_token(cur(P)))) {
    TokenType op;
    int fd;
    Word* target;
    if (!parse_one_redir(P, &op, &fd, &target)) return;
    node_group_push_redir(group, op, fd, target);
  }
}

static Node* parse_simple(Parser* P) {
  Node* s = node_new(NODE_SIMPLE);
  bool got = false;
  for (;;) {
    if (cur(P) == TOK_WORD) {
      node_simple_push_word(s, p_take_word(P));
      got = true;
      continue;
    }
    if (cur(P) == TOK_IO_NUMBER || is_redir_token(cur(P))) {
      TokenType op;
      int fd;
      Word* target;
      if (!parse_one_redir(P, &op, &fd, &target)) {
        node_free(s);
        return NULL;
      }
      node_simple_push_redir(s, op, fd, target);
      got = true;
      continue;
    }
    break;
  }
  if (!got) {
    p_error(P, "syntax error: expected a command");
    node_free(s);
    return NULL;
  }
  return s;
}

static Node* parse_command(Parser* P) {
  if (cur(P) == TOK_LPAREN) {
    p_advance(P);
    Node* body = parse_list(P, STOP_RPAREN);
    if (P->failed) return NULL;
    if (cur(P) != TOK_RPAREN) {
      p_error(P, "expected ')'");
      node_free(body);
      return NULL;
    }
    p_advance(P);
    Node* sub = node_new(NODE_SUBSHELL);
    sub->u.group.body = body;
    parse_group_redirs(P, sub);
    if (P->failed) {
      node_free(sub);
      return NULL;
    }
    return sub;
  }

  if (tok_is_reserved(&P->tok, '{')) {
    p_advance(P);
    Node* body = parse_list(P, STOP_RBRACE);
    if (P->failed) return NULL;
    if (!tok_is_reserved(&P->tok, '}')) {
      p_error(P, "expected '}'");
      node_free(body);
      return NULL;
    }
    p_advance(P);
    Node* grp = node_new(NODE_GROUP);
    grp->u.group.body = body;
    parse_group_redirs(P, grp);
    if (P->failed) {
      node_free(grp);
      return NULL;
    }
    return grp;
  }

  if (cur(P) == TOK_DLESS || cur(P) == TOK_DLESSDASH) {
    p_error(P, "here-documents are not supported yet");
    return NULL;
  }

  return parse_simple(P);
}

static Node* parse_pipeline(Parser* P) {
  bool bang = false;
  if (tok_is_reserved(&P->tok, '!')) {
    bang = true;
    p_advance(P);
  }

  Node* pl = node_new(NODE_PIPELINE);
  pl->u.pipeline.bang = bang;

  Node* first = parse_command(P);
  if (P->failed) {
    node_free(pl);
    return NULL;
  }
  node_pipeline_push(pl, first);

  while (cur(P) == TOK_PIPE) {
    p_advance(P);
    skip_newlines(P);
    Node* c = parse_command(P);
    if (P->failed) {
      node_free(pl);
      return NULL;
    }
    node_pipeline_push(pl, c);
  }
  return pl;
}

static Node* parse_and_or(Parser* P) {
  Node* first = parse_pipeline(P);
  if (P->failed) return NULL;

  Node* ao = node_new(NODE_AND_OR);
  node_and_or_push(ao, first, '\0');

  for (;;) {
    char op;
    if (cur(P) == TOK_AND_IF) op = 'a';
    else if (cur(P) == TOK_OR_IF) op = 'o';
    else break;
    p_advance(P);
    skip_newlines(P);
    Node* next = parse_pipeline(P);
    if (P->failed) {
      node_free(ao);
      return NULL;
    }
    node_and_or_push(ao, next, op);
  }
  return ao;
}

static Node* parse_list(Parser* P, ListStop stop) {
  Node* list = node_new(NODE_LIST);
  skip_newlines(P);

  while (!P->failed && !at_list_end(P, stop)) {
    Node* ao = parse_and_or(P);
    if (P->failed) {
      node_free(list);
      return NULL;
    }

    char sep = ';';
    if (cur(P) == TOK_AMP) {
      sep = '&';
      p_advance(P);
    } else if (cur(P) == TOK_SEMI) {
      p_advance(P);
    } else if (cur(P) == TOK_NEWLINE) {
      p_advance(P);
    } else if (!at_list_end(P, stop)) {
      node_free(ao);
      node_free(list);
      p_error(P, "unexpected token");
      return NULL;
    }

    node_list_push(list, ao, sep);
    skip_newlines(P);
  }

  if (P->failed) {
    node_free(list);
    return NULL;
  }
  return list;
}

bool parse_program(const char* src, Node** out, string* errmsg) {
  *out = NULL;
  *errmsg = _SLIT0;

  Parser P;
  lexer_init(&P.lex, src);
  P.failed = false;
  P.errmsg = _SLIT0;
  P.tok.type = TOK_EOF;
  P.tok.word = NULL;
  P.tok.io_number = 0;

  p_fill(&P);
  Node* list = NULL;
  if (!P.failed) list = parse_list(&P, STOP_EOF);

  if (!P.failed && cur(&P) != TOK_EOF) {
    p_error(&P, "unexpected token");
    node_free(list);
    list = NULL;
  }

  bool ok = !P.failed;
  token_free(&P.tok);
  lexer_dispose(&P.lex);

  if (!ok) {
    *errmsg = P.errmsg;
    return false;
  }

  if (list != NULL && list->u.list.n == 0) {
    node_free(list);
    list = NULL;
  }
  *out = list;
  return true;
}
