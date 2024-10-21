#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <errno.h>
#include "color.h"
#include "expr.h"
#include "execute.h"
#include "redirect.h"
#include "pipeline.h"
#include "rstring.h"
#include "array.h"

IntResult execute_pipeline(Command* first_cmd, int* result) {
  int pipefd[2], error_pipe[2], status = 0;
  Command* cmd = first_cmd;
  pid_t pid;
  int prev_fd = -1;

  if (pipe(error_pipe) == -1)
    return Err(_SLIT("Failed to create error pipe"), ERRCODE_EXEC_PIPE_FAILED);

  while (cmd != NULL && cmd->pipline_next) {
    if (cmd->pipline_next && pipe(pipefd) == -1) {
      close(error_pipe[0]);
      close(error_pipe[1]);
      return Err(_SLIT("Failed to create pipe"), ERRCODE_EXEC_PIPE_FAILED);
    }
    pid = fork();

    if (pid == 0) {
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
      exit(1);
    } else if (pid > 0) {
      close(pipefd[1]);
      if (prev_fd != -1) {
        close(prev_fd);
      }
      prev_fd = pipefd[0];
      cmd = cmd->next;
    } else {
      close(error_pipe[0]);
      close(error_pipe[1]);
      return Err(_SLIT("fork failed in pipeline"), ERRCODE_EXEC_FORK_FAILED);
    }
  }

  if (cmd != NULL) {
    pid = fork();
    if (pid == 0) {
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
      perror("execvp failed in pipeline");
      exit(1);
    } else if (pid > 0) {
      close(prev_fd);
      waitpid(pid, &status, 0);
    } else {
      close(error_pipe[0]);
      close(error_pipe[1]);
      return Err(_SLIT("fork failed in pipeline command"), ERRCODE_EXEC_FORK_FAILED);
    }
  }

  while (wait(&status) > 0);
  close(error_pipe[1]);
  char error_buf[1024];
  ssize_t error_read;
  while ((error_read = read(error_pipe[0], error_buf, sizeof(error_buf))) > 0) {
    ssize_t temp = write(STDERR_FILENO, error_buf, (size_t)error_read);
    (void)temp;
  }
  close(error_pipe[0]);

  *result = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return Ok(NULL);
}