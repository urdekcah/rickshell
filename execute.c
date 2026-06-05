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
#include <ctype.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/stat.h>
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
#include "function.h"
#include "param.h"
#include "strconv.h"
#include "rstring.h"
#include "array.h"
#include "ast.h"
#include "parser.h"
#include "expand.h"

extern VariableTable* variable_table;

/* Exit status of the most recently executed command, exposed as "$?". Updated as
 * the executor finishes each node so it is current within a multi-command line. */
int shell_last_status = 0;

/* ---- non-local control flow (break / continue / return) ---- */

typedef enum {
  FLOW_NORMAL,    /**< No pending control-flow transfer. */
  FLOW_BREAK,     /**< A break is unwinding @c flow_count loop levels. */
  FLOW_CONTINUE,  /**< A continue is unwinding to the @c flow_count-th loop. */
  FLOW_RETURN,    /**< A return is unwinding to the enclosing function call. */
} FlowKind;

static FlowKind flow_kind = FLOW_NORMAL;  /* pending control-flow request, if any */
static int      flow_count = 0;           /* loop levels targeted by break / continue */
static int      loop_depth = 0;           /* enclosing for / while / until / select loops */
static int      func_depth = 0;           /* enclosing function calls */

static bool flow_pending(void) {
  return flow_kind != FLOW_NORMAL;
}

/*
 * Applies a pending break/continue to the loop finishing an iteration. Returns
 * true when the loop must stop: always for break and return, and for a continue
 * aimed at an outer loop. A continue aimed at this loop is cleared and reported
 * as "keep iterating".
 */
static bool consume_loop_flow(void) {
  switch (flow_kind) {
    case FLOW_BREAK:
      if (flow_count > 1) flow_count--;
      else { flow_kind = FLOW_NORMAL; flow_count = 0; }
      return true;
    case FLOW_CONTINUE:
      if (flow_count > 1) { flow_count--; return true; }
      flow_kind = FLOW_NORMAL;
      flow_count = 0;
      return false;
    case FLOW_RETURN:
      return true;
    case FLOW_NORMAL:
    default:
      return false;
  }
}

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

/* ---- function calls and the break/continue/return special forms ---- */

/* True for the words handled directly by the executor instead of being looked
 * up as builtins or external commands. */
static bool is_flow_word(const string name) {
  return string__equals(name, _SLIT("break")) ||
         string__equals(name, _SLIT("continue")) ||
         string__equals(name, _SLIT("return"));
}

/* Records a break/continue/return request from an already-expanded argv. */
static void handle_flow_builtin(Command* cmd, int* result) {
  const string name = *(string*)array_get(cmd->argv, 0);
  bool has_arg = cmd->argv.size > 1;
  long long n = 0;
  bool n_ok = false;
  if (has_arg) {
    StrconvResult sr = ratoll(*(string*)array_get(cmd->argv, 1), &n);
    n_ok = !sr.is_err;
  }

  if (string__equals(name, _SLIT("return"))) {
    if (has_arg && n_ok) *result = (int)(n & 0xff);
    if (func_depth > 0) {
      flow_kind = FLOW_RETURN;
      flow_count = 0;
    }
    return;
  }

  /* break / continue are meaningful only inside a loop; elsewhere they succeed
   * as a no-op. */
  if (loop_depth == 0) { *result = 0; return; }
  int levels = (has_arg && n_ok && n >= 1) ? (int)n : 1;
  flow_kind = string__equals(name, _SLIT("break")) ? FLOW_BREAK : FLOW_CONTINUE;
  flow_count = levels;
  *result = 0;
}

/* Runs a function body with @p argv[1..] installed as the positional parameters,
 * restoring the caller's parameters afterwards. A return inside the body unwinds
 * here; a break/continue aimed at an enclosing loop is left pending. */
static IntResult call_function(Node* body, StringArray* argv, int* result) {
  StringArray new_params = create_array(sizeof(string));
  for (size_t i = 1; i < argv->size; i++) {
    string s = string__from(*(string*)array_get(*argv, i));
    array_push(&new_params, &s);
  }
  StringArray saved = params_snapshot();
  params_replace(new_params);

  func_depth++;
  IntResult r = execute_node(body, result);
  func_depth--;

  if (flow_kind == FLOW_RETURN) {
    flow_kind = FLOW_NORMAL;
    flow_count = 0;
  }

  params_replace(saved);
  return r;
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

  if (is_flow_word(argv0)) {
    handle_flow_builtin(cmd, result);
    free_command(cmd);
    return Ok(NULL);
  }

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

  /* A defined function shadows an external command of the same name; its body
   * runs in this shell with the redirections applied around it. */
  Node* fbody = find_function(argv0);
  if (fbody != NULL) {
    SavedFds saved = fds_save();
    Result rr = handle_redirection(cmd);
    IntResult fr = Ok(NULL);
    if (rr.is_err) {
      report_error(rr);
      string__free(rr.err.msg);
      *result = 1;
    } else {
      fr = call_function(fbody, &cmd->argv, result);
    }
    fflush(stdout);
    fflush(stderr);
    fds_restore(saved);
    free_command(cmd);
    return fr;
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
    if (!r.is_err && !flow_pending() && bang) *result = (*result == 0) ? 1 : 0;
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
    if (flow_pending()) break;
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
    case NODE_IF:
      string_builder__append_cstr(sb, "if ");
      describe_node(sb, node->u.if_clause.cond);
      string_builder__append_cstr(sb, "; then ...; fi");
      break;
    case NODE_WHILE:
      string_builder__append_cstr(sb, node->u.while_loop.until ? "until " : "while ");
      describe_node(sb, node->u.while_loop.cond);
      string_builder__append_cstr(sb, "; do ...; done");
      break;
    case NODE_FOR:
      string_builder__append_cstr(sb, "for ");
      string_builder__append(sb, node->u.for_loop.name);
      string_builder__append_cstr(sb, "; do ...; done");
      break;
    case NODE_SELECT:
      string_builder__append_cstr(sb, "select ");
      string_builder__append(sb, node->u.for_loop.name);
      string_builder__append_cstr(sb, "; do ...; done");
      break;
    case NODE_FOR_ARITH:
      string_builder__append_cstr(sb, "for ((...)); do ...; done");
      break;
    case NODE_CASE:
      string_builder__append_cstr(sb, "case ");
      describe_word(sb, node->u.case_stmt.subject);
      string_builder__append_cstr(sb, " in ...; esac");
      break;
    case NODE_ARITH:
      string_builder__append_cstr(sb, "((");
      string_builder__append(sb, node->u.arith.expr);
      string_builder__append_cstr(sb, "))");
      break;
    case NODE_COND:
      string_builder__append_cstr(sb, "[[ ... ]]");
      break;
    case NODE_FUNCDEF:
      string_builder__append(sb, node->u.funcdef.name);
      string_builder__append_cstr(sb, "() { ...; }");
      break;
    case NODE_REDIR:
      describe_node(sb, node->u.group.body);
      string_builder__append_cstr(sb, " >...");
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
    if (flow_pending()) break;
  }
  return Ok(NULL);
}

/* ---- [[ ]] conditional expression ---- */

/* Evaluates a unary file/string test such as "-f file" or "-z str". */
static bool eval_cond_unary(const string op, const Word* operand) {
  string s = expand_word_to_string(variable_table, operand);
  const char* p = s.str;
  struct stat st;
  bool res = false;

  if (string__equals(op, _SLIT("-z")))      res = (s.len == 0);
  else if (string__equals(op, _SLIT("-n"))) res = (s.len > 0);
  else if (string__equals(op, _SLIT("-e"))) res = (access(p, F_OK) == 0);
  else if (string__equals(op, _SLIT("-f"))) res = (stat(p, &st) == 0 && S_ISREG(st.st_mode));
  else if (string__equals(op, _SLIT("-d"))) res = (stat(p, &st) == 0 && S_ISDIR(st.st_mode));
  else if (string__equals(op, _SLIT("-r"))) res = (access(p, R_OK) == 0);
  else if (string__equals(op, _SLIT("-w"))) res = (access(p, W_OK) == 0);
  else if (string__equals(op, _SLIT("-x"))) res = (access(p, X_OK) == 0);
  else if (string__equals(op, _SLIT("-s"))) res = (stat(p, &st) == 0 && st.st_size > 0);
  else if (string__equals(op, _SLIT("-h")) || string__equals(op, _SLIT("-L")))
    res = (lstat(p, &st) == 0 && S_ISLNK(st.st_mode));
  else if (string__equals(op, _SLIT("-b"))) res = (stat(p, &st) == 0 && S_ISBLK(st.st_mode));
  else if (string__equals(op, _SLIT("-c"))) res = (stat(p, &st) == 0 && S_ISCHR(st.st_mode));
  else if (string__equals(op, _SLIT("-p"))) res = (stat(p, &st) == 0 && S_ISFIFO(st.st_mode));
  else if (string__equals(op, _SLIT("-S"))) res = (stat(p, &st) == 0 && S_ISSOCK(st.st_mode));
  else if (string__equals(op, _SLIT("-k"))) res = (stat(p, &st) == 0 && (st.st_mode & S_ISVTX) != 0);
  else if (string__equals(op, _SLIT("-u"))) res = (stat(p, &st) == 0 && (st.st_mode & S_ISUID) != 0);
  else if (string__equals(op, _SLIT("-g"))) res = (stat(p, &st) == 0 && (st.st_mode & S_ISGID) != 0);
  else if (string__equals(op, _SLIT("-t"))) {
    long long fd = 0;
    StrconvResult sr = ratoll(s, &fd);
    res = (!sr.is_err && isatty((int)fd) == 1);
  } else if (string__equals(op, _SLIT("-v"))) {
    res = (get_variable(variable_table, s) != NULL);
  }

  string__free(s);
  return res;
}

/* Evaluates a binary test: pattern match (== != =), regex (=~), lexical compare
 * (< >), arithmetic compare (-eq ...), or file relation (-nt -ot -ef). */
static bool eval_cond_binary(const string op, const Word* left, const Word* right) {
  string l = expand_word_to_string(variable_table, left);
  bool res = false;

  if (string__equals(op, _SLIT("=~"))) {
    string r = expand_word_to_string(variable_table, right);
    res = expand_regex_match(l, r);
    string__free(r);
  } else if (string__equals(op, _SLIT("==")) || string__equals(op, _SLIT("="))) {
    res = expand_pattern_match(variable_table, l, right);
  } else if (string__equals(op, _SLIT("!="))) {
    res = !expand_pattern_match(variable_table, l, right);
  } else {
    string r = expand_word_to_string(variable_table, right);
    if (string__equals(op, _SLIT("<"))) {
      res = strcmp(l.str, r.str) < 0;
    } else if (string__equals(op, _SLIT(">"))) {
      res = strcmp(l.str, r.str) > 0;
    } else if (string__equals(op, _SLIT("-nt")) || string__equals(op, _SLIT("-ot")) ||
               string__equals(op, _SLIT("-ef"))) {
      struct stat a, b;
      bool oka = (stat(l.str, &a) == 0);
      bool okb = (stat(r.str, &b) == 0);
      if (string__equals(op, _SLIT("-nt")))      res = oka && (!okb || a.st_mtime > b.st_mtime);
      else if (string__equals(op, _SLIT("-ot"))) res = okb && (!oka || a.st_mtime < b.st_mtime);
      else res = oka && okb && a.st_dev == b.st_dev && a.st_ino == b.st_ino;
    } else {
      long long a = strtoll(l.str, NULL, 10);
      long long b = strtoll(r.str, NULL, 10);
      if (string__equals(op, _SLIT("-eq")))      res = (a == b);
      else if (string__equals(op, _SLIT("-ne"))) res = (a != b);
      else if (string__equals(op, _SLIT("-lt"))) res = (a < b);
      else if (string__equals(op, _SLIT("-le"))) res = (a <= b);
      else if (string__equals(op, _SLIT("-gt"))) res = (a > b);
      else if (string__equals(op, _SLIT("-ge"))) res = (a >= b);
    }
    string__free(r);
  }

  string__free(l);
  return res;
}

static bool eval_cond(const CondNode* c) {
  if (c == NULL) return false;
  switch (c->type) {
    case COND_STRING: {
      string s = expand_word_to_string(variable_table, c->left);
      bool r = (s.len > 0);
      string__free(s);
      return r;
    }
    case COND_UNARY:  return eval_cond_unary(c->op, c->left);
    case COND_BINARY: return eval_cond_binary(c->op, c->left, c->right);
    case COND_NOT:    return !eval_cond(c->a);
    case COND_AND:    return eval_cond(c->a) && eval_cond(c->b);
    case COND_OR:     return eval_cond(c->a) || eval_cond(c->b);
  }
  return false;
}

static IntResult run_cond(Node* node, int* result) {
  *result = eval_cond(node->u.cond.expr) ? 0 : 1;
  return Ok(NULL);
}

static IntResult run_arith_cmd(Node* node, int* result) {
  long long v = 0;
  bool ok = expand_arith(variable_table, node->u.arith.expr, &v);
  *result = (ok && v != 0) ? 0 : 1;
  return Ok(NULL);
}

/* ---- compound statements ---- */

static IntResult run_if(Node* node, int* result) {
  int cond_status = 0;
  IntResult r = execute_node(node->u.if_clause.cond, &cond_status);
  NTRY(r);
  if (flow_pending()) { *result = cond_status; return Ok(NULL); }
  if (cond_status == 0)
    return execute_node(node->u.if_clause.then_body, result);
  if (node->u.if_clause.else_part != NULL)
    return execute_node(node->u.if_clause.else_part, result);
  *result = 0;
  return Ok(NULL);
}

static IntResult run_while(Node* node, int* result) {
  bool until = node->u.while_loop.until;
  *result = 0;
  loop_depth++;
  for (;;) {
    int cond_status = 0;
    IntResult r = execute_node(node->u.while_loop.cond, &cond_status);
    if (r.is_err) { loop_depth--; return r; }
    if (flow_pending()) break;
    bool go = until ? (cond_status != 0) : (cond_status == 0);
    if (!go) break;
    r = execute_node(node->u.while_loop.body, result);
    if (r.is_err) { loop_depth--; return r; }
    if (consume_loop_flow()) break;
  }
  loop_depth--;
  return Ok(NULL);
}

/* Builds a for/select loop's iteration list from its words, or from the
 * positional parameters when the "in" list was omitted. Moves expanded fields
 * into the result; the caller releases it with free_loop_items(). */
static StringArray build_loop_items(Node* node) {
  StringArray items = create_array(sizeof(string));
  if (node->u.for_loop.have_in) {
    for (size_t i = 0; i < node->u.for_loop.nwords; i++) {
      StringArray fields = expand_word_to_fields(variable_table, node->u.for_loop.words[i]);
      for (size_t j = 0; j < fields.size; j++)
        array_push(&items, array_get(fields, j));
      array_free(&fields);
    }
  } else {
    StringArray all = params_snapshot();
    for (size_t j = 0; j < all.size; j++)
      array_push(&items, array_get(all, j));
    array_free(&all);
  }
  return items;
}

static void free_loop_items(StringArray* items) {
  for (size_t i = 0; i < items->size; i++)
    string__free(*(string*)array_get(*items, i));
  array_free(items);
}

static IntResult run_for(Node* node, int* result) {
  *result = 0;
  StringArray items = build_loop_items(node);
  loop_depth++;
  for (size_t i = 0; i < items.size; i++) {
    set_variable(variable_table, node->u.for_loop.name,
                 *(string*)array_get(items, i), VAR_STRING, false);
    IntResult r = execute_node(node->u.for_loop.body, result);
    if (r.is_err) { loop_depth--; free_loop_items(&items); return r; }
    if (consume_loop_flow()) break;
  }
  loop_depth--;
  free_loop_items(&items);
  return Ok(NULL);
}

static IntResult run_for_arith(Node* node, int* result) {
  *result = 0;
  long long scratch = 0;
  if (node->u.for_arith.init.len > 0)
    expand_arith(variable_table, node->u.for_arith.init, &scratch);
  loop_depth++;
  for (;;) {
    if (node->u.for_arith.cond.len > 0) {
      long long cv = 0;
      bool ok = expand_arith(variable_table, node->u.for_arith.cond, &cv);
      if (!ok || cv == 0) break;
    }
    IntResult r = execute_node(node->u.for_arith.body, result);
    if (r.is_err) { loop_depth--; return r; }
    if (consume_loop_flow()) break;
    if (node->u.for_arith.update.len > 0)
      expand_arith(variable_table, node->u.for_arith.update, &scratch);
  }
  loop_depth--;
  return Ok(NULL);
}

static IntResult run_case(Node* node, int* result) {
  *result = 0;
  string subject = expand_word_to_string(variable_table, node->u.case_stmt.subject);
  bool fall = false;  /* set by ";&" to run the next clause's body unconditionally */
  for (size_t i = 0; i < node->u.case_stmt.nitems; i++) {
    CaseItem* item = &node->u.case_stmt.items[i];
    bool hit = fall;
    if (!hit) {
      for (size_t j = 0; j < item->npatterns; j++) {
        if (expand_pattern_match(variable_table, subject, item->patterns[j])) {
          hit = true;
          break;
        }
      }
    }
    if (!hit) continue;
    IntResult r = execute_node(item->body, result);
    if (r.is_err) { string__free(subject); return r; }
    if (flow_pending()) break;
    if (item->terminator == 1) { fall = true; continue; }   /* ";&"  fall through */
    if (item->terminator == 2) { fall = false; continue; }  /* ";;&" keep testing */
    break;                                                  /* ";;"  done */
  }
  string__free(subject);
  return Ok(NULL);
}

static IntResult run_select(Node* node, int* result) {
  *result = 0;
  StringArray items = build_loop_items(node);
  if (items.size == 0) { free_loop_items(&items); return Ok(NULL); }

  loop_depth++;
  bool show_menu = true;
  for (;;) {
    if (show_menu) {
      for (size_t i = 0; i < items.size; i++)
        fprintf(stderr, "%zu) %s\n", i + 1, ((string*)array_get(items, i))->str);
      show_menu = false;
    }
    fputs("#? ", stderr);
    fflush(stderr);

    char line[1024];
    if (fgets(line, sizeof(line), stdin) == NULL) { fputc('\n', stderr); break; }
    size_t ll = strlen(line);
    if (ll > 0 && line[ll - 1] == '\n') line[--ll] = '\0';

    string sline = string__new(line);
    set_variable(variable_table, _SLIT("REPLY"), sline, VAR_STRING, false);
    if (ll == 0) { string__free(sline); show_menu = true; continue; }

    long long sel = 0;
    StrconvResult sr = ratoll(sline, &sel);
    string__free(sline);

    string name_val = (!sr.is_err && sel >= 1 && sel <= (long long)items.size)
                          ? string__from(*(string*)array_get(items, (size_t)(sel - 1)))
                          : string__new("");
    set_variable(variable_table, node->u.for_loop.name, name_val, VAR_STRING, false);
    string__free(name_val);

    IntResult r = execute_node(node->u.for_loop.body, result);
    if (r.is_err) { loop_depth--; free_loop_items(&items); return r; }
    if (consume_loop_flow()) break;
  }
  loop_depth--;
  free_loop_items(&items);
  return Ok(NULL);
}

static IntResult run_funcdef(Node* node, int* result) {
  define_function(node->u.funcdef.name, node->u.funcdef.body);
  *result = 0;
  return Ok(NULL);
}

IntResult execute_node(Node* node, int* result) {
  if (node == NULL) return Ok(NULL);
  IntResult r = Ok(NULL);
  switch (node->type) {
    case NODE_LIST:      r = run_list(node, result); break;
    case NODE_AND_OR:    r = run_and_or(node, result); break;
    case NODE_PIPELINE:  r = run_pipeline(node, result); break;
    case NODE_SIMPLE:    r = run_simple(node, result); break;
    case NODE_SUBSHELL:  r = run_subshell(node, result); break;
    case NODE_GROUP:     r = run_group(node, result); break;
    case NODE_IF:        r = run_if(node, result); break;
    case NODE_FOR:       r = run_for(node, result); break;
    case NODE_FOR_ARITH: r = run_for_arith(node, result); break;
    case NODE_WHILE:     r = run_while(node, result); break;
    case NODE_CASE:      r = run_case(node, result); break;
    case NODE_SELECT:    r = run_select(node, result); break;
    case NODE_ARITH:     r = run_arith_cmd(node, result); break;
    case NODE_COND:      r = run_cond(node, result); break;
    case NODE_FUNCDEF:   r = run_funcdef(node, result); break;
    case NODE_REDIR:     r = run_group(node, result); break;
  }
  /* Track the most recent command status so "$?" reflects it mid-line. */
  if (!r.is_err) shell_last_status = *result;
  return r;
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
