/*
 * parser.c — Recursive-descent parser over the lexer's token stream.
 *
 * The parser keeps exactly one token of lookahead in Parser.tok and consumes it
 * with p_advance(). Word ownership moves into the tree via p_take_word(), which
 * detaches the word from the lookahead so the subsequent advance does not free
 * it; any word the parser steps over without taking (a reserved word such as
 * "if", "then", "{" or "!") is released by p_advance(). On the first error the
 * parser records a message and unwinds, freeing every partially built node on
 * the way out.
 *
 * Reserved words are recognized only in command position — the start of a
 * command and the start of a compound-list element — which is where the keyword
 * dispatch and the list-terminator checks live. Elsewhere "if", "done" and the
 * like are ordinary words, so `echo done` keeps working.
 */
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "parser.h"
#include "ast.h"
#include "lexer.h"
#include "word.h"
#include "rstring.h"

typedef struct {
  Lexer  lex;
  Token  tok;        /* current lookahead */
  bool   failed;
  bool   incomplete; /* failure was end-of-input mid-construct; more input would help */
  string errmsg;     /* owned once failed is true */
} Parser;

/* Terminator keyword tables for the compound-list bodies. */
static const char* const KW_THEN[]    = { "then" };
static const char* const KW_IF_TAIL[] = { "elif", "else", "fi" };
static const char* const KW_FI[]      = { "fi" };
static const char* const KW_DO[]      = { "do" };
static const char* const KW_DONE[]    = { "done" };
static const char* const KW_ESAC[]    = { "esac" };
static const char* const KW_RBRACE[]  = { "}" };

#define TERMS(arr) (arr), (sizeof(arr) / sizeof((arr)[0]))

/* Unary and binary operators recognized inside "[[ ]]". */
static const char* const COND_UNARY_OPS[] = {
  "-e", "-f", "-d", "-r", "-w", "-x", "-s", "-z", "-n", "-h", "-L",
  "-b", "-c", "-p", "-S", "-t", "-o", "-v", "-R", "-G", "-O", "-N",
  "-k", "-u", "-g",
};
static const char* const COND_BIN_OPS[] = {
  "==", "=", "!=", "=~", "-eq", "-ne", "-lt", "-le", "-gt", "-ge",
  "-nt", "-ot", "-ef",
};

static Node* parse_list(Parser* P, bool stop_rparen, bool stop_dsemi,
                        const char* const* terms, size_t nterms);
static Node* parse_command(Parser* P);
static CondNode* parse_cond_or(Parser* P);

/* ---- lookahead plumbing ---- */

static void p_fill(Parser* P) {
  if (!lexer_next(&P->lex, &P->tok)) {
    P->failed = true;
    if (P->lex.incomplete) P->incomplete = true;
    if (P->errmsg.len == 0) P->errmsg = string__from(P->lex.errmsg);
  }
}

static void p_advance(Parser* P) {
  if (P->tok.word != NULL) {
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

/* Records "expected `what'", flagging incompleteness when the input simply ran
 * out before the expected token. */
static void p_error_expect(Parser* P, const char* what) {
  if (P->failed) return;
  if (P->tok.type == TOK_EOF) P->incomplete = true;
  char buf[96];
  snprintf(buf, sizeof(buf), "expected `%s'", what);
  p_error(P, buf);
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

/* True when the lookahead is the unquoted single-segment literal word @p kw,
 * the form in which a reserved word reaches the parser. */
static bool tok_is_word(const Token* t, const char* kw) {
  if (t->type != TOK_WORD || t->word == NULL || t->word->size != 1) return false;
  const WordSeg* s = &t->word->data[0];
  if (s->kind != SEG_LITERAL || s->quoted) return false;
  size_t kwlen = strlen(kw);
  return s->text.len == kwlen && memcmp(s->text.str, kw, kwlen) == 0;
}

/* True when @p w is a single unquoted literal that forms a valid name (a leading
 * letter or '_' followed by letters, digits, or '_'). */
static bool word_is_name(const Word* w) {
  if (w == NULL || w->size != 1) return false;
  const WordSeg* s = &w->data[0];
  if (s->kind != SEG_LITERAL || s->quoted) return false;
  const char* t = s->text.str;
  size_t n = s->text.len;
  if (n == 0) return false;
  if (!(t[0] == '_' || isalpha((unsigned char)t[0]))) return false;
  for (size_t i = 1; i < n; i++)
    if (!(t[i] == '_' || isalnum((unsigned char)t[i]))) return false;
  return true;
}

/* Extracts the owned name string from a name word and frees the word. */
static string word_to_name(Word* w) {
  string name = string__from(w->data[0].text);
  word_free(w);
  return name;
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

/* Decides whether the current token ends the compound list being parsed. */
static bool at_terminator(const Parser* P, bool stop_rparen, bool stop_dsemi,
                          const char* const* terms, size_t nterms) {
  TokenType t = cur(P);
  if (t == TOK_EOF) return true;
  if (stop_rparen && t == TOK_RPAREN) return true;
  if (stop_dsemi && (t == TOK_DSEMI || t == TOK_SEMI_AMP || t == TOK_DSEMI_AMP)) return true;
  for (size_t i = 0; i < nterms; i++)
    if (tok_is_word(&P->tok, terms[i])) return true;
  return false;
}

/* Consumes @p kw when present, otherwise records an "expected `kw'" error. */
static bool expect_keyword(Parser* P, const char* kw) {
  if (tok_is_word(&P->tok, kw)) {
    p_advance(P);
    return true;
  }
  p_error_expect(P, kw);
  return false;
}

/* ---- redirections ---- */

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

/* Consumes redirections that trail a subshell, brace group, or compound command. */
static void parse_group_redirs(Parser* P, Node* group) {
  while (!P->failed && (cur(P) == TOK_IO_NUMBER || is_redir_token(cur(P)))) {
    TokenType op;
    int fd;
    Word* target;
    if (!parse_one_redir(P, &op, &fd, &target)) return;
    node_group_push_redir(group, op, fd, target);
  }
}

/* Wraps a just-parsed compound command in a NODE_REDIR when redirections trail
 * it, so they apply to the whole construct (for example "done > log"). When no
 * redirection follows, @p cmd is returned unchanged. */
static Node* wrap_compound_redirs(Parser* P, Node* cmd) {
  if (P->failed || cmd == NULL) return cmd;
  if (cur(P) != TOK_IO_NUMBER && !is_redir_token(cur(P))) return cmd;
  Node* rn = node_new(NODE_REDIR);
  rn->u.group.body = cmd;
  parse_group_redirs(P, rn);
  if (P->failed) { node_free(rn); return NULL; }
  return rn;
}

/* ---- simple command ---- */

/* Parses a simple command, optionally seeded with an already-taken first word. */
static Node* parse_simple_from(Parser* P, Word* first) {
  Node* s = node_new(NODE_SIMPLE);
  bool got = false;
  if (first != NULL) {
    node_simple_push_word(s, first);
    got = true;
  }
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
    if (cur(P) == TOK_EOF) P->incomplete = true;
    p_error(P, "syntax error: expected a command");
    node_free(s);
    return NULL;
  }
  return s;
}

/* ---- if / elif / else ---- */

/* Parses the part following "if" or "elif": a condition, "then", a body, and
 * then an elif chain, an else clause, or "fi". An elif chain shares the single
 * closing "fi" with the enclosing if. */
static Node* parse_if_rest(Parser* P) {
  Node* node = node_new(NODE_IF);
  node->u.if_clause.cond = parse_list(P, false, false, TERMS(KW_THEN));
  if (P->failed) { node_free(node); return NULL; }
  if (!expect_keyword(P, "then")) { node_free(node); return NULL; }
  node->u.if_clause.then_body = parse_list(P, false, false, TERMS(KW_IF_TAIL));
  if (P->failed) { node_free(node); return NULL; }

  if (tok_is_word(&P->tok, "elif")) {
    p_advance(P);
    node->u.if_clause.else_part = parse_if_rest(P);  /* recursion consumes the "fi" */
    if (P->failed) { node_free(node); return NULL; }
    return node;
  }
  if (tok_is_word(&P->tok, "else")) {
    p_advance(P);
    node->u.if_clause.else_part = parse_list(P, false, false, TERMS(KW_FI));
    if (P->failed) { node_free(node); return NULL; }
  }
  if (!expect_keyword(P, "fi")) { node_free(node); return NULL; }
  return node;
}

static Node* parse_if(Parser* P) {
  p_advance(P);  /* consume "if" */
  return parse_if_rest(P);
}

/* ---- while / until ---- */

/* Parses "do compound_list done", with the cursor on "do". */
static Node* parse_do_group(Parser* P) {
  if (!expect_keyword(P, "do")) return NULL;
  Node* body = parse_list(P, false, false, TERMS(KW_DONE));
  if (P->failed) return NULL;
  if (!expect_keyword(P, "done")) { node_free(body); return NULL; }
  return body;
}

static Node* parse_while(Parser* P, bool until) {
  p_advance(P);  /* consume "while" / "until" */
  Node* node = node_new(NODE_WHILE);
  node->u.while_loop.until = until;
  node->u.while_loop.cond = parse_list(P, false, false, TERMS(KW_DO));
  if (P->failed) { node_free(node); return NULL; }
  node->u.while_loop.body = parse_do_group(P);
  if (P->failed) { node_free(node); return NULL; }
  return node;
}

/* ---- for / select ---- */

/* Parses the "name [in words]; do list; done" form shared by for and select. */
static Node* parse_for_in(Parser* P, NodeType type) {
  if (cur(P) != TOK_WORD || !word_is_name(P->tok.word)) {
    if (cur(P) == TOK_EOF) P->incomplete = true;
    p_error(P, "expected a name after `for' or `select'");
    return NULL;
  }
  Node* node = node_new(type);
  node->u.for_loop.name = word_to_name(p_take_word(P));

  if (tok_is_word(&P->tok, "in")) {
    node->u.for_loop.have_in = true;
    p_advance(P);
    while (cur(P) == TOK_WORD && !tok_is_word(&P->tok, "do"))
      node_for_push_word(node, p_take_word(P));
  }
  while (cur(P) == TOK_SEMI || cur(P) == TOK_NEWLINE) p_advance(P);

  node->u.for_loop.body = parse_do_group(P);
  if (P->failed) { node_free(node); return NULL; }
  return node;
}

/* Splits the body of a C-style for header into its three arithmetic clauses. */
static bool split_for_arith(Parser* P, const string src, string* init, string* cond, string* upd) {
  ssize_t s1 = string__indexof(src, _SLIT(";"));
  if (s1 < 0) {
    p_error(P, "malformed `for ((...))': expected two `;'");
    return false;
  }
  string after1 = string__substring(src, s1 + 1);
  ssize_t s2 = string__indexof(after1, _SLIT(";"));
  if (s2 < 0) {
    string__free(after1);
    p_error(P, "malformed `for ((...))': expected two `;'");
    return false;
  }
  string p0 = string__substring(src, 0, s1);
  string p1 = string__substring(after1, 0, s2);
  string p2 = string__substring(after1, s2 + 1);
  string__free(after1);
  *init = string__trim(p0);
  *cond = string__trim(p1);
  *upd  = string__trim(p2);
  string__free(p0);
  string__free(p1);
  string__free(p2);
  return true;
}

static Node* parse_for_arith(Parser* P) {
  Node* node = node_new(NODE_FOR_ARITH);
  string combined = (P->tok.word != NULL && P->tok.word->size > 0)
                        ? string__from(P->tok.word->data[0].text)
                        : string__new("");
  p_advance(P);  /* consume the (( ... )) token */
  if (!split_for_arith(P, combined, &node->u.for_arith.init,
                        &node->u.for_arith.cond, &node->u.for_arith.update)) {
    string__free(combined);
    node_free(node);
    return NULL;
  }
  string__free(combined);

  while (cur(P) == TOK_SEMI || cur(P) == TOK_NEWLINE) p_advance(P);
  node->u.for_arith.body = parse_do_group(P);
  if (P->failed) { node_free(node); return NULL; }
  return node;
}

static Node* parse_for(Parser* P) {
  p_advance(P);  /* consume "for" */
  if (cur(P) == TOK_ARITH) return parse_for_arith(P);
  return parse_for_in(P, NODE_FOR);
}

static Node* parse_select(Parser* P) {
  p_advance(P);  /* consume "select" */
  return parse_for_in(P, NODE_SELECT);
}

/* ---- case ---- */

static Node* parse_case(Parser* P) {
  p_advance(P);  /* consume "case" */
  if (cur(P) != TOK_WORD) {
    if (cur(P) == TOK_EOF) P->incomplete = true;
    p_error(P, "expected a word after `case'");
    return NULL;
  }
  Node* node = node_new(NODE_CASE);
  node->u.case_stmt.subject = p_take_word(P);
  skip_newlines(P);
  if (!expect_keyword(P, "in")) { node_free(node); return NULL; }
  skip_newlines(P);

  while (!P->failed && !tok_is_word(&P->tok, "esac")) {
    if (cur(P) == TOK_EOF) { p_error_expect(P, "esac"); break; }

    CaseItem* item = node_case_push_item(node);
    if (cur(P) == TOK_LPAREN) p_advance(P);  /* optional leading "(" */

    for (;;) {
      if (cur(P) != TOK_WORD || tok_is_word(&P->tok, "esac")) {
        if (cur(P) == TOK_EOF) P->incomplete = true;
        p_error(P, "expected a case pattern");
        break;
      }
      case_item_push_pattern(item, p_take_word(P));
      if (cur(P) == TOK_PIPE) { p_advance(P); continue; }
      break;
    }
    if (P->failed) break;

    if (cur(P) != TOK_RPAREN) { p_error(P, "expected `)' after a case pattern"); break; }
    p_advance(P);  /* consume ")" */

    item->body = parse_list(P, false, true, TERMS(KW_ESAC));
    if (P->failed) break;

    if (cur(P) == TOK_DSEMI)          { item->terminator = 0; p_advance(P); }
    else if (cur(P) == TOK_SEMI_AMP)  { item->terminator = 1; p_advance(P); }
    else if (cur(P) == TOK_DSEMI_AMP) { item->terminator = 2; p_advance(P); }
    else if (tok_is_word(&P->tok, "esac")) { item->terminator = 0; }
    else {
      if (cur(P) == TOK_EOF) P->incomplete = true;
      p_error(P, "expected `;;' or `esac'");
      break;
    }
    skip_newlines(P);
  }

  if (P->failed) { node_free(node); return NULL; }
  if (!expect_keyword(P, "esac")) { node_free(node); return NULL; }
  return node;
}

/* ---- [[ conditional expression ]] ---- */

/* Takes the word operand expected inside "[[ ]]", rejecting the "]]" terminator. */
static Word* take_cond_operand(Parser* P) {
  if (cur(P) != TOK_WORD || tok_is_word(&P->tok, "]]")) {
    if (cur(P) == TOK_EOF) P->incomplete = true;
    p_error(P, "expected an operand in `[[ ]]'");
    return NULL;
  }
  return p_take_word(P);
}

static CondNode* parse_cond_primary(Parser* P) {
  if (cur(P) == TOK_LPAREN) {
    p_advance(P);
    CondNode* e = parse_cond_or(P);
    if (P->failed) { cond_free(e); return NULL; }
    if (cur(P) != TOK_RPAREN) {
      if (cur(P) == TOK_EOF) P->incomplete = true;
      p_error(P, "expected `)' in `[[ ]]'");
      cond_free(e);
      return NULL;
    }
    p_advance(P);
    return e;
  }

  for (size_t i = 0; i < sizeof(COND_UNARY_OPS) / sizeof(COND_UNARY_OPS[0]); i++) {
    if (tok_is_word(&P->tok, COND_UNARY_OPS[i])) {
      CondNode* c = cond_new(COND_UNARY);
      c->op = string__new(COND_UNARY_OPS[i]);
      p_advance(P);
      c->left = take_cond_operand(P);
      if (P->failed) { cond_free(c); return NULL; }
      return c;
    }
  }

  Word* left = take_cond_operand(P);
  if (P->failed) return NULL;

  const char* binop = NULL;
  if (cur(P) == TOK_LESS) binop = "<";
  else if (cur(P) == TOK_GREAT) binop = ">";
  else {
    for (size_t i = 0; i < sizeof(COND_BIN_OPS) / sizeof(COND_BIN_OPS[0]); i++) {
      if (tok_is_word(&P->tok, COND_BIN_OPS[i])) { binop = COND_BIN_OPS[i]; break; }
    }
  }

  if (binop != NULL) {
    CondNode* c = cond_new(COND_BINARY);
    c->op = string__new(binop);
    c->left = left;
    p_advance(P);
    c->right = take_cond_operand(P);
    if (P->failed) { cond_free(c); return NULL; }
    return c;
  }

  CondNode* c = cond_new(COND_STRING);
  c->left = left;
  return c;
}

static CondNode* parse_cond_not(Parser* P) {
  if (tok_is_word(&P->tok, "!")) {
    p_advance(P);
    CondNode* child = parse_cond_not(P);
    if (P->failed) { cond_free(child); return NULL; }
    CondNode* c = cond_new(COND_NOT);
    c->a = child;
    return c;
  }
  return parse_cond_primary(P);
}

static CondNode* parse_cond_and(Parser* P) {
  CondNode* left = parse_cond_not(P);
  if (P->failed) return NULL;
  while (cur(P) == TOK_AND_IF) {
    p_advance(P);
    skip_newlines(P);
    CondNode* right = parse_cond_not(P);
    if (P->failed) { cond_free(left); return NULL; }
    CondNode* c = cond_new(COND_AND);
    c->a = left;
    c->b = right;
    left = c;
  }
  return left;
}

static CondNode* parse_cond_or(Parser* P) {
  CondNode* left = parse_cond_and(P);
  if (P->failed) return NULL;
  while (cur(P) == TOK_OR_IF) {
    p_advance(P);
    skip_newlines(P);
    CondNode* right = parse_cond_and(P);
    if (P->failed) { cond_free(left); return NULL; }
    CondNode* c = cond_new(COND_OR);
    c->a = left;
    c->b = right;
    left = c;
  }
  return left;
}

static Node* parse_cond(Parser* P) {
  p_advance(P);  /* consume "[[" */
  Node* node = node_new(NODE_COND);
  node->u.cond.expr = parse_cond_or(P);
  if (P->failed) { node_free(node); return NULL; }
  if (!expect_keyword(P, "]]")) { node_free(node); return NULL; }
  return node;
}

/* ---- (( arithmetic command )) ---- */

static Node* parse_arith_cmd(Parser* P) {
  Node* node = node_new(NODE_ARITH);
  node->u.arith.expr = (P->tok.word != NULL && P->tok.word->size > 0)
                           ? string__from(P->tok.word->data[0].text)
                           : string__new("");
  p_advance(P);  /* consume the (( ... )) token */
  return node;
}

/* ---- function definitions ---- */

/* Parses a compound command to serve as a function body. A simple command is
 * not a valid body. */
static Node* parse_function_body(Parser* P) {
  skip_newlines(P);
  Node* body = parse_command(P);
  if (P->failed) return NULL;
  if (body->type == NODE_SIMPLE) {
    p_error(P, "a function body must be a compound command");
    node_free(body);
    return NULL;
  }
  return body;
}

/* POSIX "name () compound-command" form, entered with @p name already taken and
 * the cursor on "(". */
static Node* parse_funcdef(Parser* P, string name) {
  p_advance(P);  /* consume "(" */
  if (cur(P) != TOK_RPAREN) {
    p_error(P, "expected `)' in a function definition");
    string__free(name);
    return NULL;
  }
  p_advance(P);  /* consume ")" */
  Node* body = parse_function_body(P);
  if (P->failed) { string__free(name); return NULL; }
  Node* node = node_new(NODE_FUNCDEF);
  node->u.funcdef.name = name;
  node->u.funcdef.body = body;
  return node;
}

/* "function name [()] compound-command" form. */
static Node* parse_function_kw(Parser* P) {
  p_advance(P);  /* consume "function" */
  if (cur(P) != TOK_WORD || !word_is_name(P->tok.word)) {
    if (cur(P) == TOK_EOF) P->incomplete = true;
    p_error(P, "expected a function name after `function'");
    return NULL;
  }
  string name = word_to_name(p_take_word(P));
  if (cur(P) == TOK_LPAREN) {
    p_advance(P);
    if (cur(P) != TOK_RPAREN) {
      p_error(P, "expected `)' in a function definition");
      string__free(name);
      return NULL;
    }
    p_advance(P);
  }
  Node* body = parse_function_body(P);
  if (P->failed) { string__free(name); return NULL; }
  Node* node = node_new(NODE_FUNCDEF);
  node->u.funcdef.name = name;
  node->u.funcdef.body = body;
  return node;
}

/* ---- command, pipeline, and-or, list ---- */

static Node* parse_command(Parser* P) {
  if (cur(P) == TOK_ARITH) return wrap_compound_redirs(P, parse_arith_cmd(P));

  if (tok_is_word(&P->tok, "if"))       return wrap_compound_redirs(P, parse_if(P));
  if (tok_is_word(&P->tok, "while"))    return wrap_compound_redirs(P, parse_while(P, false));
  if (tok_is_word(&P->tok, "until"))    return wrap_compound_redirs(P, parse_while(P, true));
  if (tok_is_word(&P->tok, "for"))      return wrap_compound_redirs(P, parse_for(P));
  if (tok_is_word(&P->tok, "case"))     return wrap_compound_redirs(P, parse_case(P));
  if (tok_is_word(&P->tok, "select"))   return wrap_compound_redirs(P, parse_select(P));
  if (tok_is_word(&P->tok, "function")) return parse_function_kw(P);
  if (tok_is_word(&P->tok, "[["))       return wrap_compound_redirs(P, parse_cond(P));

  if (cur(P) == TOK_LPAREN) {
    p_advance(P);
    Node* body = parse_list(P, true, false, NULL, 0);
    if (P->failed) return NULL;
    if (cur(P) != TOK_RPAREN) {
      p_error_expect(P, ")");
      node_free(body);
      return NULL;
    }
    p_advance(P);
    Node* sub = node_new(NODE_SUBSHELL);
    sub->u.group.body = body;
    parse_group_redirs(P, sub);
    if (P->failed) { node_free(sub); return NULL; }
    return sub;
  }

  if (tok_is_word(&P->tok, "{")) {
    p_advance(P);
    Node* body = parse_list(P, false, false, TERMS(KW_RBRACE));
    if (P->failed) return NULL;
    if (!expect_keyword(P, "}")) { node_free(body); return NULL; }
    Node* grp = node_new(NODE_GROUP);
    grp->u.group.body = body;
    parse_group_redirs(P, grp);
    if (P->failed) { node_free(grp); return NULL; }
    return grp;
  }

  if (cur(P) == TOK_DLESS || cur(P) == TOK_DLESSDASH) {
    p_error(P, "here-documents are not supported yet");
    return NULL;
  }

  if (cur(P) == TOK_WORD) {
    Word* first = p_take_word(P);
    if (cur(P) == TOK_LPAREN && word_is_name(first))
      return parse_funcdef(P, word_to_name(first));
    return parse_simple_from(P, first);
  }

  return parse_simple_from(P, NULL);
}

static Node* parse_pipeline(Parser* P) {
  bool bang = false;
  if (tok_is_word(&P->tok, "!")) {
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

static Node* parse_list(Parser* P, bool stop_rparen, bool stop_dsemi,
                        const char* const* terms, size_t nterms) {
  Node* list = node_new(NODE_LIST);
  skip_newlines(P);

  while (!P->failed && !at_terminator(P, stop_rparen, stop_dsemi, terms, nterms)) {
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
    } else if (!at_terminator(P, stop_rparen, stop_dsemi, terms, nterms)) {
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

/* ---- entry points ---- */

static bool parse_run(const char* src, Node** out, string* errmsg, bool* incomplete) {
  *out = NULL;
  *errmsg = _SLIT0;
  *incomplete = false;

  Parser P;
  lexer_init(&P.lex, src);
  P.failed = false;
  P.incomplete = false;
  P.errmsg = _SLIT0;
  P.tok.type = TOK_EOF;
  P.tok.word = NULL;
  P.tok.io_number = 0;

  p_fill(&P);
  Node* list = NULL;
  if (!P.failed) list = parse_list(&P, false, false, NULL, 0);

  if (!P.failed && cur(&P) != TOK_EOF) {
    p_error(&P, "unexpected token");
    node_free(list);
    list = NULL;
  }

  bool ok = !P.failed;
  *incomplete = P.incomplete || P.lex.incomplete;
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

bool parse_program(const char* src, Node** out, string* errmsg) {
  bool incomplete = false;
  return parse_run(src, out, errmsg, &incomplete);
}

bool parse_input_incomplete(const char* src) {
  Node* tree = NULL;
  string err = _SLIT0;
  bool incomplete = false;
  bool ok = parse_run(src, &tree, &err, &incomplete);
  node_free(tree);
  if (!ok) string__free(err);
  return incomplete;
}
