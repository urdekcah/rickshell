/*
 * execute.c — Tree-walking executor over the parsed syntax tree.
 *
 * The walker handles control flow (sequencing, background, and-or short
 * circuiting, subshells, brace groups) and lowers each simple command and
 * pipeline onto the lower-level execution layer: a transient Command carries the
 * fully expanded argv and redirections to handle_redirection() and
 * execute_pipeline(), which own the process-group and terminal handling.
 * Expansion happens here, once, before a Command is built, so nothing
 * downstream re-expands.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "expr.h"
#include "builtin.h"
#include "pipeline.h"
#include "redirect.h"
#include "execute.h"
#include "io.h"
#include "job.h"
#include "memory.h"
#include "error.h"
#include "variable.h"
#include "strconv.h"
#include "rstring.h"
#include "array.h"
#include "ast.h"
#include "parser.h"
#include "expand.h"

extern VariableTable* variable_table;

int rexecvp(const string __file, StringArray __argv) {
  char* file_cstr = string__to_cstr(__file);
  if (!file_cstr) {
    errno = ENOMEM;
    return -1;
  }

  size_t argc = __argv.size;
  register size_t i;

  char** argv_cstr = malloc((argc + 1) * sizeof(char*));
  if (!argv_cstr) {
    free(file_cstr);
    errno = ENOMEM;
    return -1;
  }

  for (i = 0; i < argc; i++) {
    string* arg = array_get(__argv, i);
    argv_cstr[i] = string__to_cstr(*arg);
    if (!argv_cstr[i]) {
      for (size_t j = 0; j < i; j++)
        free(argv_cstr[j]);
      free(argv_cstr);
      free(file_cstr);
      errno = ENOMEM;
      return -1;
    }
  }
  argv_cstr[argc] = NULL;
  int result = execvp(file_cstr, argv_cstr);

  int saved_errno = errno;

  for (i = 0; i < argc; i++)
    free(argv_cstr[i]);
  free(argv_cstr);
  free(file_cstr);

  errno = saved_errno;
  return result;
}

IntResult execute_node(Node* node, int* result);

/* ---- redirection lowering ---- */

static bool map_redir_type(TokenType op, RedirectType* rt, int* deffd) {
  switch (op) {
    case TOK_LESS:       *rt = REDIRECT_INPUT;       *deffd = STDIN_FILENO;  return true;
    case TOK_GREAT:
    case TOK_CLOBBER:    *rt = REDIRECT_OUTPUT;      *deffd = STDOUT_FILENO; return true;
    case TOK_DGREAT:     *rt = REDIRECT_APPEND;      *deffd = STDOUT_FILENO; return true;
    case TOK_LESSAND:    *rt = REDIRECT_INPUT_DUP;   *deffd = STDIN_FILENO;  return true;
    case TOK_GREATAND:   *rt = REDIRECT_OUTPUT_DUP;  *deffd = STDOUT_FILENO; return true;
    case TOK_DGREATAND:  *rt = REDIRECT_APPEND_DUP;  *deffd = STDOUT_FILENO; return true;
    default:             return false;
  }
}

/* Expands each redirection target and records it on @p cmd in execution form. */
static void lower_redirs(Command* cmd, const Redir* redirs, size_t n) {
  for (size_t i = 0; i < n; i++) {
    const Redir* r = &redirs[i];
    string tgt = expand_word_to_string(variable_table, r->target);
    if (r->op == TOK_LESSGREAT) {
      add_redirect(cmd, REDIRECT_INPUT, STDIN_FILENO, tgt);
      add_redirect(cmd, REDIRECT_OUTPUT, STDOUT_FILENO, tgt);
    } else {
      RedirectType rt;
      int deffd;
      if (map_redir_type(r->op, &rt, &deffd)) {
        int fd = (r->fd >= 0) ? r->fd : deffd;
        add_redirect(cmd, rt, fd, tgt);
      }
    }
    string__free(tgt);
  }
}

/* ---- saved standard descriptors for in-process redirection ---- */

typedef struct {
  int fd0;
  int fd1;
  int fd2;
} SavedFds;

static SavedFds fds_save(void) {
  SavedFds s;
  s.fd0 = dup(STDIN_FILENO);
  s.fd1 = dup(STDOUT_FILENO);
  s.fd2 = dup(STDERR_FILENO);
  return s;
}

static void fds_restore(SavedFds s) {
  if (s.fd0 != -1) { dup2(s.fd0, STDIN_FILENO);  close(s.fd0); }
  if (s.fd1 != -1) { dup2(s.fd1, STDOUT_FILENO); close(s.fd1); }
  if (s.fd2 != -1) { dup2(s.fd2, STDERR_FILENO); close(s.fd2); }
}

/* ---- assignments ---- */

static bool is_name_char(char c) {
  return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

/* True when @p w has the form name=..., name[key]=... or name+=... in its first
 * unquoted literal segment. */
static bool word_is_assignment(const Word* w) {
  if (w->size == 0) return false;
  const WordSeg* s0 = &w->data[0];
  if (s0->kind != SEG_LITERAL || s0->quoted) return false;
  const char* t = s0->text.str;
  size_t n = s0->text.len;
  if (n == 0) return false;
  if (!(t[0] == '_' || (t[0] >= 'a' && t[0] <= 'z') || (t[0] >= 'A' && t[0] <= 'Z'))) return false;
  size_t i = 1;
  while (i < n && is_name_char(t[i])) i++;
  if (i < n && t[i] == '[') {
    int depth = 1;
    i++;
    while (i < n && depth > 0) {
      if (t[i] == '[') depth++;
      else if (t[i] == ']') depth--;
      i++;
    }
  }
  if (i < n && t[i] == '+') i++;
  return i < n && t[i] == '=';
}

static void apply_assignment(VariableTable* table, const string name, const string value) {
  ssize_t ob = string__indexof(name, _SLIT("["));
  ssize_t cb = string__lastindexof(name, _SLIT("]"));
  if (ob != -1 && cb != -1 && ob < cb) {
    string base = string__substring(name, 0, ob);
    string key = string__substring(name, ob + 1, cb);
    Variable* var = get_variable(table, base);
    if (var != NULL && var->value.type == VAR_ASSOCIATIVE_ARRAY) {
      set_associative_array_variable(table, base, key, value);
    } else if (var != NULL && var->value.type == VAR_ARRAY) {
      long long idx;
      StrconvResult sr = ratoll(key, &idx);
      if (!sr.is_err && idx >= 0)
        array_set_element(table, base, (size_t)idx, value);
    }
    string__free(base);
    string__free(key);
    return;
  }
  if (string__startswith(value, _SLIT("(")) && string__endswith(value, _SLIT(")"))) {
    parse_and_set_array(table, name, value);
    return;
  }
  if (string__startswith(value, _SLIT("{")) && string__endswith(value, _SLIT("}"))) {
    parse_and_set_associative_array(table, name, value);
    return;
  }
  set_variable(table, name, value, parse_variable_type(value), false);
}

/* Applies one assignment word, expanding its right-hand side without splitting. */
static void apply_assignment_word(VariableTable* table, const Word* w) {
  const string t0 = w->data[0].text;
  ssize_t eq = string__indexof(t0, _SLIT("="));
  if (eq < 0) return;
  ssize_t nameend = eq;
  if (nameend > 0 && t0.str[nameend - 1] == '+') nameend--;
  string name = string__substring(t0, 0, nameend);

  Word* vw = word_new();
  string rem = string__substring(t0, eq + 1);
  word_push(vw, SEG_LITERAL, rem, false);
  for (size_t k = 1; k < w->size; k++)
    word_push(vw, w->data[k].kind, string__from(w->data[k].text), w->data[k].quoted);
  string value = expand_word_to_string(table, vw);
  word_free(vw);

  apply_assignment(table, name, value);
  string__free(name);
  string__free(value);
}

/* ---- simple-command construction ---- */

static void move_fields(Command* cmd, StringArray* fields) {
  for (size_t i = 0; i < fields->size; i++) {
    string* f = array_get(*fields, i);
    array_push(&cmd->argv, f);
  }
  array_free(fields);
}

/*
 * Expands a simple command's words into @p cmd->argv and lowers its
 * redirections. Leading assignment words are removed from the argument list;
 * when @p standalone they are applied to the shell, otherwise (a pipeline
 * stage) they are dropped since a forked stage cannot persist them.
 */
static void build_simple(Node* node, Command* cmd, bool standalone) {
  Word** words = node->u.simple.words;
  size_t nwords = node->u.simple.nwords;

  size_t wi = 0;
  while (wi < nwords && word_is_assignment(words[wi])) {
    if (standalone) apply_assignment_word(variable_table, words[wi]);
    wi++;
  }

  bool assign_builtin = false;
  for (; wi < nwords; wi++) {
    Word* w = words[wi];
    if (cmd->argv.size == 0) {
      StringArray fields = expand_word_to_fields(variable_table, w);
      move_fields(cmd, &fields);
      if (cmd->argv.size > 0)
        assign_builtin = do_not_expand_this_builtin(*(string*)array_get(cmd->argv, 0));
    } else if (assign_builtin && word_is_assignment(w)) {
      string s = expand_word_to_string(variable_table, w);
      array_push(&cmd->argv, &s);
    } else {
      StringArray fields = expand_word_to_fields(variable_table, w);
      move_fields(cmd, &fields);
    }
  }

  lower_redirs(cmd, node->u.simple.redirs, node->u.simple.nredirs);
}

/* ---- command substitution ---- */

string command_substitution(const string src) {
  int pfd[2];
  if (pipe(pfd) == -1) return string__new("");

  /* Flush before forking so inherited buffered output is not captured into the
   * substitution result. */
  fflush(NULL);
  pid_t pid = fork();
  if (pid == 0) {
    close(pfd[0]);
    dup2(pfd[1], STDOUT_FILENO);
    close(pfd[1]);
    reset_child_signals();

    Node* tree = NULL;
    string err = _SLIT0;
    int st = 0;
    if (parse_program(src.str, &tree, &err)) {
      if (tree != NULL) {
        execute_node(tree, &st);
        node_free(tree);
      }
    } else {
      print_error(err);
      string__free(err);
      st = 1;
    }
    fflush(NULL);
    _exit(st & 0xff);
  } else if (pid > 0) {
    close(pfd[1]);
    StringBuilder sb = string_builder__new();
    char buf[4096];
    ssize_t rd;
    while ((rd = read(pfd[0], buf, sizeof(buf) - 1)) > 0) {
      buf[rd] = '\0';
      string_builder__append_cstr(&sb, buf);
    }
    close(pfd[0]);
    int status;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) { }
    string out = string_builder__to_string(&sb);
    string_builder__free(&sb);
    return out;
  }

  close(pfd[0]);
  close(pfd[1]);
  return string__new("");
}

/* ---- node executors ---- */

static IntResult run_simple(Node* node, int* result) {
  Command* cmd = create_command();
  build_simple(node, cmd, true);

  if (cmd->argv.size == 0) {
    if (cmd->redirects.size > 0) {
      SavedFds saved = fds_save();
      Result r = handle_redirection(cmd);
      fds_restore(saved);
      if (r.is_err) {
        report_error(r);
        string__free(r.err.msg);
        *result = 1;
      } else {
        *result = 0;
      }
    } else {
      *result = 0;
    }
    free_command(cmd);
    return Ok(NULL);
  }

  string argv0 = *(string*)array_get(cmd->argv, 0);
  if (get_builtin_func(argv0) != NULL) {
    SavedFds saved = fds_save();
    Result r = handle_redirection(cmd);
    if (r.is_err) {
      report_error(r);
      string__free(r.err.msg);
      *result = 1;
    } else {
      *result = execute_builtin(cmd);
    }
    /* Flush the builtin's buffered output while the redirection is still in
     * effect; restoring the descriptors first would send it to the terminal. */
    fflush(stdout);
    fflush(stderr);
    fds_restore(saved);
    free_command(cmd);
    return Ok(NULL);
  }

  Result r = execute_pipeline(cmd, result);
  free_command(cmd);
  NTRY(r);
  return Ok(NULL);
}

static IntResult run_pipeline(Node* node, int* result) {
  size_t cnt = node->u.pipeline.n;
  bool bang = node->u.pipeline.bang;

  if (cnt == 1) {
    IntResult r = execute_node(node->u.pipeline.cmds[0], result);
    if (!r.is_err && bang) *result = (*result == 0) ? 1 : 0;
    return r;
  }

  Command* head = NULL;
  for (size_t i = 0; i < cnt; i++) {
    Node* st = node->u.pipeline.cmds[i];
    if (st->type != NODE_SIMPLE) {
      for (Command* c = head; c != NULL;) {
        Command* nx = c->next;
        free_command(c);
        c = nx;
      }
      return Err(_SLIT("compound command in a pipeline is not supported yet"), ERRCODE_EXEC_FAILED);
    }
    Command* c = create_command();
    build_simple(st, c, false);
    if (head == NULL) head = c;
    else add_pipeline(head, c);
  }

  Result r = execute_pipeline(head, result);
  for (Command* c = head; c != NULL;) {
    Command* nx = c->next;
    free_command(c);
    c = nx;
  }
  if (!r.is_err && bang) *result = (*result == 0) ? 1 : 0;
  NTRY(r);
  return Ok(NULL);
}

static IntResult run_subshell(Node* node, int* result) {
  int fg = job_control_active();
  /* Drain buffered output before forking so the child does not inherit and
   * re-emit it. */
  fflush(NULL);
  pid_t pid = fork();
  if (pid == -1)
    return Err(_SLIT("Fork failed"), ERRCODE_EXEC_FORK_FAILED);

  if (pid == 0) {
    if (fg) {
      setpgid(0, 0);
      give_terminal_to(getpid());
    }
    reset_child_signals();
    if (node->u.group.nredirs > 0) {
      Command* rc = create_command();
      lower_redirs(rc, node->u.group.redirs, node->u.group.nredirs);
      Result rr = handle_redirection(rc);
      free_command(rc);
      if (rr.is_err) {
        report_error(rr);
        _exit(1);
      }
    }
    int st = 0;
    execute_node(node->u.group.body, &st);
    fflush(NULL);  /* flush builtin output before _exit, which does not */
    _exit(st & 0xff);
  }

  if (fg) {
    setpgid(pid, pid);
    give_terminal_to(pid);
  }
  int status;
  if (waitpid(pid, &status, 0) == -1) {
    if (fg) reclaim_terminal();
    return Err(_SLIT("Waitpid failed"), ERRCODE_EXEC_WAIT_FAILED);
  }
  if (fg) reclaim_terminal();

  if (WIFEXITED(status)) {
    *result = WEXITSTATUS(status);
  } else if (WIFSIGNALED(status)) {
    int sig = WTERMSIG(status);
    if (sig == SIGINT) {
      ssize_t w = write(STDOUT_FILENO, "\n", 1);
      (void)w;
    }
    *result = 128 + sig;
  } else {
    *result = -1;
  }
  return Ok(NULL);
}

static IntResult run_group(Node* node, int* result) {
  bool redir = node->u.group.nredirs > 0;
  Command* rc = NULL;
  SavedFds saved = {-1, -1, -1};

  if (redir) {
    rc = create_command();
    lower_redirs(rc, node->u.group.redirs, node->u.group.nredirs);
    saved = fds_save();
    Result rr = handle_redirection(rc);
    if (rr.is_err) {
      report_error(rr);
      string__free(rr.err.msg);
      fds_restore(saved);
      free_command(rc);
      *result = 1;
      return Ok(NULL);
    }
  }

  IntResult r = execute_node(node->u.group.body, result);

  if (redir) {
    /* Flush buffered output from the group's builtins before the descriptors
     * are restored. */
    fflush(stdout);
    fflush(stderr);
    fds_restore(saved);
    free_command(rc);
  }
  return r;
}

static IntResult run_and_or(Node* node, int* result) {
  size_t cnt = node->u.and_or.n;
  for (size_t i = 0; i < cnt; i++) {
    if (i > 0) {
      char op = node->u.and_or.ops[i];
      if (op == 'a' && *result != 0) continue;
      if (op == 'o' && *result == 0) continue;
    }
    IntResult r = execute_node(node->u.and_or.items[i], result);
    NTRY(r);
  }
  return Ok(NULL);
}

/* Reconstructs an approximate source line for the jobs list display. */
static void describe_word(StringBuilder* sb, const Word* w) {
  for (size_t i = 0; i < w->size; i++) {
    const WordSeg* s = &w->data[i];
    switch (s->kind) {
      case SEG_LITERAL: string_builder__append(sb, s->text); break;
      case SEG_PARAM:   string_builder__append(sb, s->text); break;
      case SEG_ARITH:
        string_builder__append_cstr(sb, "$((");
        string_builder__append(sb, s->text);
        string_builder__append_cstr(sb, "))");
        break;
      case SEG_COMMAND:
        string_builder__append_cstr(sb, "$(");
        string_builder__append(sb, s->text);
        string_builder__append_cstr(sb, ")");
        break;
    }
  }
}

static void describe_node(StringBuilder* sb, const Node* node) {
  switch (node->type) {
    case NODE_SIMPLE:
      for (size_t i = 0; i < node->u.simple.nwords; i++) {
        if (i > 0) string_builder__append_char(sb, ' ');
        describe_word(sb, node->u.simple.words[i]);
      }
      break;
    case NODE_PIPELINE:
      if (node->u.pipeline.bang) string_builder__append_cstr(sb, "! ");
      for (size_t i = 0; i < node->u.pipeline.n; i++) {
        if (i > 0) string_builder__append_cstr(sb, " | ");
        describe_node(sb, node->u.pipeline.cmds[i]);
      }
      break;
    case NODE_AND_OR:
      for (size_t i = 0; i < node->u.and_or.n; i++) {
        if (i > 0)
          string_builder__append_cstr(sb, node->u.and_or.ops[i] == 'a' ? " && " : " || ");
        describe_node(sb, node->u.and_or.items[i]);
      }
      break;
    case NODE_LIST:
      for (size_t i = 0; i < node->u.list.n; i++) {
        if (i > 0) string_builder__append_cstr(sb, "; ");
        describe_node(sb, node->u.list.items[i]);
      }
      break;
    case NODE_SUBSHELL:
      string_builder__append_char(sb, '(');
      describe_node(sb, node->u.group.body);
      string_builder__append_char(sb, ')');
      break;
    case NODE_GROUP:
      string_builder__append_cstr(sb, "{ ");
      describe_node(sb, node->u.group.body);
      string_builder__append_cstr(sb, "; }");
      break;
  }
}

static string node_describe(const Node* node) {
  StringBuilder sb = string_builder__new();
  describe_node(&sb, node);
  string r = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return r;
}

static IntResult run_list(Node* node, int* result) {
  size_t cnt = node->u.list.n;
  for (size_t i = 0; i < cnt; i++) {
    Node* item = node->u.list.items[i];
    if (node->u.list.seps[i] == '&') {
      string desc = node_describe(item);
      Result r = execute_background_job(item, desc, result);
      string__free(desc);
      NTRY(r);
    } else {
      IntResult r = execute_node(item, result);
      NTRY(r);
    }
  }
  return Ok(NULL);
}

IntResult execute_node(Node* node, int* result) {
  if (node == NULL) return Ok(NULL);
  switch (node->type) {
    case NODE_LIST:     return run_list(node, result);
    case NODE_AND_OR:   return run_and_or(node, result);
    case NODE_PIPELINE: return run_pipeline(node, result);
    case NODE_SIMPLE:   return run_simple(node, result);
    case NODE_SUBSHELL: return run_subshell(node, result);
    case NODE_GROUP:    return run_group(node, result);
  }
  return Ok(NULL);
}

IntResult parse_and_execute(const string input, int* result) {
  if (string__is_null_or_empty(input))
    return Err(_SLIT("Invalid input"), ERRCODE_INVALID_INPUT);

  *result = 0;
  Node* tree = NULL;
  string err = _SLIT0;
  if (!parse_program(input.str, &tree, &err)) {
    print_error(err);
    string__free(err);
    *result = 2;
    return Ok(NULL);
  }

  if (tree == NULL)
    return Ok(NULL);

  IntResult r = execute_node(tree, result);
  node_free(tree);
  return r;
}
