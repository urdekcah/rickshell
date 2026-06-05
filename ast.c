/*
 * ast.c — Allocation, growth, and recursive teardown of syntax-tree nodes.
 *
 * The parser builds the tree through the small push helpers here; the executor
 * only reads it. Keeping all ownership transfer in one place makes node_free()
 * the single authority on what a node owns, so freeing a partially built tree
 * after a parse error cannot leak or double-free.
 */
#include <stdbool.h>
#include <stddef.h>
#include "ast.h"
#include "lexer.h"
#include "word.h"
#include "memory.h"

Node* node_new(NodeType type) {
  Node* n = rcalloc(1, sizeof(Node));
  n->type = type;
  return n;
}

static void free_redirs(Redir* redirs, size_t n) {
  for (size_t i = 0; i < n; i++)
    word_free(redirs[i].target);
  rfree(redirs);
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
      for (size_t i = 0; i < n->u.simple.nwords; i++)
        word_free(n->u.simple.words[i]);
      rfree(n->u.simple.words);
      free_redirs(n->u.simple.redirs, n->u.simple.nredirs);
      break;
    case NODE_SUBSHELL:
    case NODE_GROUP:
      node_free(n->u.group.body);
      free_redirs(n->u.group.redirs, n->u.group.nredirs);
      break;
  }
  rfree(n);
}

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
