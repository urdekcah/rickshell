/*
 * ast.c — Allocation, growth, deep copy, and recursive teardown of syntax-tree
 * nodes.
 *
 * The parser builds the tree through the small push helpers here; the executor
 * only reads it. Keeping all ownership transfer in one place makes node_free()
 * the single authority on what a node owns, so freeing a partially built tree
 * after a parse error cannot leak or double-free. node_clone() mirrors that
 * authority for deep copies, which is how a function body is detached from the
 * input line that defined it and kept for later calls.
 */
#include <stdbool.h>
#include <stddef.h>
#include "ast.h"
#include "lexer.h"
#include "word.h"
#include "rstring.h"
#include "memory.h"

Node* node_new(NodeType type) {
  Node* n = rcalloc(1, sizeof(Node));
  n->type = type;
  return n;
}

/* ---- teardown ---- */

static void free_redirs(Redir* redirs, size_t n) {
  for (size_t i = 0; i < n; i++)
    word_free(redirs[i].target);
  rfree(redirs);
}

static void free_word_array(Word** words, size_t n) {
  for (size_t i = 0; i < n; i++)
    word_free(words[i]);
  rfree(words);
}

static void free_case_item(CaseItem* item) {
  free_word_array(item->patterns, item->npatterns);
  node_free(item->body);
}

void cond_free(CondNode* c) {
  if (c == NULL) return;
  string__free(c->op);
  word_free(c->left);
  word_free(c->right);
  cond_free(c->a);
  cond_free(c->b);
  rfree(c);
}

void node_free(Node* n) {
  if (n == NULL) return;
  switch (n->type) {
    case NODE_LIST:
      for (size_t i = 0; i < n->u.list.n; i++)
        node_free(n->u.list.items[i]);
      rfree(n->u.list.items);
      rfree(n->u.list.seps);
      break;
    case NODE_AND_OR:
      for (size_t i = 0; i < n->u.and_or.n; i++)
        node_free(n->u.and_or.items[i]);
      rfree(n->u.and_or.items);
      rfree(n->u.and_or.ops);
      break;
    case NODE_PIPELINE:
      for (size_t i = 0; i < n->u.pipeline.n; i++)
        node_free(n->u.pipeline.cmds[i]);
      rfree(n->u.pipeline.cmds);
      break;
    case NODE_SIMPLE:
      free_word_array(n->u.simple.words, n->u.simple.nwords);
      free_redirs(n->u.simple.redirs, n->u.simple.nredirs);
      break;
    case NODE_SUBSHELL:
    case NODE_GROUP:
    case NODE_REDIR:
      node_free(n->u.group.body);
      free_redirs(n->u.group.redirs, n->u.group.nredirs);
      break;
    case NODE_IF:
      node_free(n->u.if_clause.cond);
      node_free(n->u.if_clause.then_body);
      node_free(n->u.if_clause.else_part);
      break;
    case NODE_FOR:
    case NODE_SELECT:
      string__free(n->u.for_loop.name);
      free_word_array(n->u.for_loop.words, n->u.for_loop.nwords);
      node_free(n->u.for_loop.body);
      break;
    case NODE_FOR_ARITH:
      string__free(n->u.for_arith.init);
      string__free(n->u.for_arith.cond);
      string__free(n->u.for_arith.update);
      node_free(n->u.for_arith.body);
      break;
    case NODE_WHILE:
      node_free(n->u.while_loop.cond);
      node_free(n->u.while_loop.body);
      break;
    case NODE_CASE:
      word_free(n->u.case_stmt.subject);
      for (size_t i = 0; i < n->u.case_stmt.nitems; i++)
        free_case_item(&n->u.case_stmt.items[i]);
      rfree(n->u.case_stmt.items);
      break;
    case NODE_ARITH:
      string__free(n->u.arith.expr);
      break;
    case NODE_COND:
      cond_free(n->u.cond.expr);
      break;
    case NODE_FUNCDEF:
      string__free(n->u.funcdef.name);
      node_free(n->u.funcdef.body);
      break;
  }
  rfree(n);
}

/* ---- deep copy ---- */

static Redir* clone_redirs(const Redir* src, size_t n) {
  if (n == 0) return NULL;
  Redir* out = rcalloc(n, sizeof(Redir));
  for (size_t i = 0; i < n; i++) {
    out[i].op = src[i].op;
    out[i].fd = src[i].fd;
    out[i].target = word_clone(src[i].target);
  }
  return out;
}

static Word** clone_word_array(Word** src, size_t n) {
  if (n == 0) return NULL;
  Word** out = rcalloc(n, sizeof(Word*));
  for (size_t i = 0; i < n; i++)
    out[i] = word_clone(src[i]);
  return out;
}

static Node** clone_node_array(Node** src, size_t n) {
  if (n == 0) return NULL;
  Node** out = rcalloc(n, sizeof(Node*));
  for (size_t i = 0; i < n; i++)
    out[i] = node_clone(src[i]);
  return out;
}

CondNode* cond_clone(const CondNode* c) {
  if (c == NULL) return NULL;
  CondNode* out = cond_new(c->type);
  out->op = string__from(c->op);
  out->left = word_clone(c->left);
  out->right = word_clone(c->right);
  out->a = cond_clone(c->a);
  out->b = cond_clone(c->b);
  return out;
}

Node* node_clone(const Node* n) {
  if (n == NULL) return NULL;
  Node* out = node_new(n->type);
  switch (n->type) {
    case NODE_LIST:
      out->u.list.n = n->u.list.n;
      out->u.list.items = clone_node_array(n->u.list.items, n->u.list.n);
      if (n->u.list.n > 0) {
        out->u.list.seps = rcalloc(n->u.list.n, sizeof(char));
        for (size_t i = 0; i < n->u.list.n; i++)
          out->u.list.seps[i] = n->u.list.seps[i];
      }
      break;
    case NODE_AND_OR:
      out->u.and_or.n = n->u.and_or.n;
      out->u.and_or.items = clone_node_array(n->u.and_or.items, n->u.and_or.n);
      if (n->u.and_or.n > 0) {
        out->u.and_or.ops = rcalloc(n->u.and_or.n, sizeof(char));
        for (size_t i = 0; i < n->u.and_or.n; i++)
          out->u.and_or.ops[i] = n->u.and_or.ops[i];
      }
      break;
    case NODE_PIPELINE:
      out->u.pipeline.n = n->u.pipeline.n;
      out->u.pipeline.bang = n->u.pipeline.bang;
      out->u.pipeline.cmds = clone_node_array(n->u.pipeline.cmds, n->u.pipeline.n);
      break;
    case NODE_SIMPLE:
      out->u.simple.nwords = n->u.simple.nwords;
      out->u.simple.words = clone_word_array(n->u.simple.words, n->u.simple.nwords);
      out->u.simple.nredirs = n->u.simple.nredirs;
      out->u.simple.redirs = clone_redirs(n->u.simple.redirs, n->u.simple.nredirs);
      break;
    case NODE_SUBSHELL:
    case NODE_GROUP:
    case NODE_REDIR:
      out->u.group.body = node_clone(n->u.group.body);
      out->u.group.nredirs = n->u.group.nredirs;
      out->u.group.redirs = clone_redirs(n->u.group.redirs, n->u.group.nredirs);
      break;
    case NODE_IF:
      out->u.if_clause.cond = node_clone(n->u.if_clause.cond);
      out->u.if_clause.then_body = node_clone(n->u.if_clause.then_body);
      out->u.if_clause.else_part = node_clone(n->u.if_clause.else_part);
      break;
    case NODE_FOR:
    case NODE_SELECT:
      out->u.for_loop.name = string__from(n->u.for_loop.name);
      out->u.for_loop.nwords = n->u.for_loop.nwords;
      out->u.for_loop.have_in = n->u.for_loop.have_in;
      out->u.for_loop.words = clone_word_array(n->u.for_loop.words, n->u.for_loop.nwords);
      out->u.for_loop.body = node_clone(n->u.for_loop.body);
      break;
    case NODE_FOR_ARITH:
      out->u.for_arith.init = string__from(n->u.for_arith.init);
      out->u.for_arith.cond = string__from(n->u.for_arith.cond);
      out->u.for_arith.update = string__from(n->u.for_arith.update);
      out->u.for_arith.body = node_clone(n->u.for_arith.body);
      break;
    case NODE_WHILE:
      out->u.while_loop.cond = node_clone(n->u.while_loop.cond);
      out->u.while_loop.body = node_clone(n->u.while_loop.body);
      out->u.while_loop.until = n->u.while_loop.until;
      break;
    case NODE_CASE:
      out->u.case_stmt.subject = word_clone(n->u.case_stmt.subject);
      out->u.case_stmt.nitems = n->u.case_stmt.nitems;
      if (n->u.case_stmt.nitems > 0) {
        out->u.case_stmt.items = rcalloc(n->u.case_stmt.nitems, sizeof(CaseItem));
        for (size_t i = 0; i < n->u.case_stmt.nitems; i++) {
          const CaseItem* si = &n->u.case_stmt.items[i];
          CaseItem* di = &out->u.case_stmt.items[i];
          di->npatterns = si->npatterns;
          di->patterns = clone_word_array(si->patterns, si->npatterns);
          di->body = node_clone(si->body);
          di->terminator = si->terminator;
        }
      }
      break;
    case NODE_ARITH:
      out->u.arith.expr = string__from(n->u.arith.expr);
      break;
    case NODE_COND:
      out->u.cond.expr = cond_clone(n->u.cond.expr);
      break;
    case NODE_FUNCDEF:
      out->u.funcdef.name = string__from(n->u.funcdef.name);
      out->u.funcdef.body = node_clone(n->u.funcdef.body);
      break;
  }
  return out;
}

/* ---- conditional-expression nodes ---- */

CondNode* cond_new(CondType type) {
  CondNode* c = rcalloc(1, sizeof(CondNode));
  c->type = type;
  return c;
}

/* ---- list / and-or / pipeline / simple builders ---- */

void node_list_push(Node* list, Node* item, char sep) {
  size_t i = list->u.list.n;
  list->u.list.items = rrealloc(list->u.list.items, (i + 1) * sizeof(Node*));
  list->u.list.seps = rrealloc(list->u.list.seps, (i + 1) * sizeof(char));
  list->u.list.items[i] = item;
  list->u.list.seps[i] = sep;
  list->u.list.n = i + 1;
}

void node_and_or_push(Node* andor, Node* pipeline, char op) {
  size_t i = andor->u.and_or.n;
  andor->u.and_or.items = rrealloc(andor->u.and_or.items, (i + 1) * sizeof(Node*));
  andor->u.and_or.ops = rrealloc(andor->u.and_or.ops, (i + 1) * sizeof(char));
  andor->u.and_or.items[i] = pipeline;
  andor->u.and_or.ops[i] = op;
  andor->u.and_or.n = i + 1;
}

void node_pipeline_push(Node* pipeline, Node* cmd) {
  size_t i = pipeline->u.pipeline.n;
  pipeline->u.pipeline.cmds = rrealloc(pipeline->u.pipeline.cmds, (i + 1) * sizeof(Node*));
  pipeline->u.pipeline.cmds[i] = cmd;
  pipeline->u.pipeline.n = i + 1;
}

void node_simple_push_word(Node* simple, Word* word) {
  size_t i = simple->u.simple.nwords;
  simple->u.simple.words = rrealloc(simple->u.simple.words, (i + 1) * sizeof(Word*));
  simple->u.simple.words[i] = word;
  simple->u.simple.nwords = i + 1;
}

static void push_redir(Redir** redirs, size_t* count, TokenType op, int fd, Word* target) {
  size_t i = *count;
  *redirs = rrealloc(*redirs, (i + 1) * sizeof(Redir));
  (*redirs)[i].op = op;
  (*redirs)[i].fd = fd;
  (*redirs)[i].target = target;
  *count = i + 1;
}

void node_simple_push_redir(Node* simple, TokenType op, int fd, Word* target) {
  push_redir(&simple->u.simple.redirs, &simple->u.simple.nredirs, op, fd, target);
}

void node_group_push_redir(Node* group, TokenType op, int fd, Word* target) {
  push_redir(&group->u.group.redirs, &group->u.group.nredirs, op, fd, target);
}

/* ---- for / select / case builders ---- */

void node_for_push_word(Node* loop, Word* word) {
  size_t i = loop->u.for_loop.nwords;
  loop->u.for_loop.words = rrealloc(loop->u.for_loop.words, (i + 1) * sizeof(Word*));
  loop->u.for_loop.words[i] = word;
  loop->u.for_loop.nwords = i + 1;
}

CaseItem* node_case_push_item(Node* casenode) {
  size_t i = casenode->u.case_stmt.nitems;
  casenode->u.case_stmt.items =
      rrealloc(casenode->u.case_stmt.items, (i + 1) * sizeof(CaseItem));
  CaseItem* item = &casenode->u.case_stmt.items[i];
  item->patterns = NULL;
  item->npatterns = 0;
  item->body = NULL;
  item->terminator = 0;
  casenode->u.case_stmt.nitems = i + 1;
  return item;
}

void case_item_push_pattern(CaseItem* item, Word* pat) {
  size_t i = item->npatterns;
  item->patterns = rrealloc(item->patterns, (i + 1) * sizeof(Word*));
  item->patterns[i] = pat;
  item->npatterns = i + 1;
}
