#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "expr.h"
#include "execute.h"
#include "redirect.h"
#include "pipeline.h"
#include "rstring.h"
#include "array.h"

int execute_pipeline(Command* first_cmd) {
  int pipefd[2], status = 0;
  Command* cmd = first_cmd;
  pid_t pid;
  int prev_fd = -1;

  while (cmd != NULL && cmd->pipline_next) {
    if (pipe(pipefd) == -1) {
      perror("pipe failed");
      return -1;
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

      if (handle_redirection(cmd) == -1) {
        exit(1);
      }
      rexecvp(*(string*)array_checked_get(cmd->argv, 0), cmd->argv);
      perror("execvp failed in pipeline");
      exit(1);
    } else if (pid > 0) {
      close(pipefd[1]);
      if (prev_fd != -1) {
        close(prev_fd);
      }
      prev_fd = pipefd[0];
      cmd = cmd->next;
    } else {
      perror("fork failed in pipeline");
      return -1;
    }
  }

  if (cmd != NULL) {
    pid = fork();
    if (pid == 0) {
      if (prev_fd != -1) {
        dup2(prev_fd, STDIN_FILENO);
        close(prev_fd);
      }
      if (handle_redirection(cmd) == -1) {
        exit(1);
      }
      rexecvp(*(string*)array_checked_get(cmd->argv, 0), cmd->argv);
      perror("execvp failed in pipeline");
      exit(1);
    } else if (pid > 0) {
      close(prev_fd);
      waitpid(pid, &status, 0);
    } else {
      perror("fork failed in last pipeline command");
      return -1;
    }
  }

  return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}