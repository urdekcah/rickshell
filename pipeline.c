#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <signal.h>
#include <errno.h>
#include "color.h"
#include "expr.h"
#include "execute.h"
#include "redirect.h"
#include "pipeline.h"
#include "job.h"
#include "memory.h"
#include "rstring.h"
#include "array.h"

IntResult execute_pipeline(Command* first_cmd, int* result) {
  int pipefd[2], error_pipe[2];
  Command* cmd = first_cmd;
  pid_t pid;
  int prev_fd = -1;

  /* All commands in the pipeline share one process group so that a single
   * Ctrl-C reaches every member at once; when this is the interactive
   * foreground shell that group also receives the controlling terminal, which
   * keeps the SIGINT away from the shell itself. */
  int fg = job_control_active();
  pid_t pgid = 0;

  size_t n = 0;
  for (Command* c = first_cmd; c != NULL; c = c->next) {
    n++;
    if (!c->pipline_next) break;
  }
  pid_t* pids = rmalloc(sizeof(pid_t) * n);
  size_t npids = 0;
  int fork_failed = 0;

  if (pipe(error_pipe) == -1) {
    rfree(pids);
    return Err(_SLIT("Failed to create error pipe"), ERRCODE_EXEC_PIPE_FAILED);
  }

  while (cmd != NULL && cmd->pipline_next) {
    if (pipe(pipefd) == -1) {
      close(error_pipe[0]);
      close(error_pipe[1]);
      if (prev_fd != -1) close(prev_fd);
      rfree(pids);
      return Err(_SLIT("Failed to create pipe"), ERRCODE_EXEC_PIPE_FAILED);
    }
    pid = fork();

    if (pid == 0) {
      if (fg) {
        pid_t target = (pgid == 0) ? getpid() : pgid;
        setpgid(0, target);
        give_terminal_to(target);
      }
      reset_child_signals();
      close(error_pipe[0]);
      if (prev_fd != -1) {
        dup2(prev_fd, STDIN_FILENO);
        close(prev_fd);
      }
      dup2(pipefd[1], STDOUT_FILENO);
      close(pipefd[0]);
      close(pipefd[1]);

      Result r = handle_redirection(cmd);
      if (r.is_err) {
        dprintf(error_pipe[1], "Error: Failed to handle redirection\n");
        _exit(1);
      }
      rexecvp(*(string*)array_checked_get(cmd->argv, 0), cmd->argv);
      dprintf(error_pipe[1], ANSI_COLOR_BRIGHT_BOLD_RED "error:" ANSI_COLOR_RESET " %s\n", strerror(errno));
      _exit(1);
    } else if (pid > 0) {
      if (pgid == 0) pgid = pid;
      if (fg) {
        setpgid(pid, pgid);
        if (npids == 0) give_terminal_to(pgid);
      }
      pids[npids++] = pid;
      close(pipefd[1]);
      if (prev_fd != -1) {
        close(prev_fd);
      }
      prev_fd = pipefd[0];
      cmd = cmd->next;
    } else {
      close(pipefd[0]);
      close(pipefd[1]);
      fork_failed = 1;
      break;
    }
  }

  if (!fork_failed && cmd != NULL) {
    pid = fork();
    if (pid == 0) {
      if (fg) {
        pid_t target = (pgid == 0) ? getpid() : pgid;
        setpgid(0, target);
        give_terminal_to(target);
      }
      reset_child_signals();
      close(error_pipe[0]);
      if (prev_fd != -1) {
        dup2(prev_fd, STDIN_FILENO);
        close(prev_fd);
      }
      Result r = handle_redirection(cmd);
      if (r.is_err) {
        dprintf(error_pipe[1], ANSI_COLOR_BRIGHT_BOLD_RED "error:" ANSI_COLOR_RESET "%s\n", r.err.msg.str);
        _exit(1);
      }
      rexecvp(*(string*)array_checked_get(cmd->argv, 0), cmd->argv);
      dprintf(error_pipe[1], ANSI_COLOR_BRIGHT_BOLD_RED "error:" ANSI_COLOR_RESET " %s\n", strerror(errno));
      _exit(1);
    } else if (pid > 0) {
      if (pgid == 0) pgid = pid;
      if (fg) {
        setpgid(pid, pgid);
        if (npids == 0) give_terminal_to(pgid);
      }
      pids[npids++] = pid;
      if (prev_fd != -1) close(prev_fd);
    } else {
      if (prev_fd != -1) close(prev_fd);
      fork_failed = 1;
    }
  }

  close(error_pipe[1]);

  /* Wait for the children we actually started, by explicit pid, so a concurrent
   * background job is never reaped here by mistake. The pipeline's exit status
   * is the status of its last command. */
  int status = 0, wstatus = 0;
  pid_t last_pid = (npids > 0) ? pids[npids - 1] : -1;
  for (size_t k = 0; k < npids; k++) {
    pid_t w;
    do { w = waitpid(pids[k], &wstatus, 0); } while (w < 0 && errno == EINTR);
    if (w == pids[k] && pids[k] == last_pid)
      status = wstatus;
  }

  if (fg) reclaim_terminal();

  char error_buf[1024];
  ssize_t error_read;
  while ((error_read = read(error_pipe[0], error_buf, sizeof(error_buf))) > 0) {
    ssize_t temp = write(STDERR_FILENO, error_buf, (size_t)error_read);
    (void)temp;
  }
  close(error_pipe[0]);
  rfree(pids);

  if (npids == 0) {
    *result = -1;
  } else if (WIFEXITED(status)) {
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

  if (fork_failed)
    return Err(_SLIT("fork failed in pipeline"), ERRCODE_EXEC_FORK_FAILED);
  return Ok(NULL);
}