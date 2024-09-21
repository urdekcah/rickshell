#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include "expr.h"
#include "redirect.h"
#include "pipeline.h"

int execute_pipeline(Command* cmd) {
  int pipefd[2];
  pid_t pid;
  int status = 0;
  int in_fd = STDIN_FILENO;

  while (cmd != NULL) {
    if (cmd->next != NULL && pipe(pipefd) == -1) {
      perror("pipe");
      return -1;
    }

    pid = fork();
    if (pid == 0) {
      if (in_fd != STDIN_FILENO) {
        if (dup2(in_fd, STDIN_FILENO) == -1) {
          perror("dup2");
          exit(EXIT_FAILURE);
        }
        close(in_fd);
      }

      if (cmd->next != NULL) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
          perror("dup2");
          exit(EXIT_FAILURE);
        }
        close(pipefd[1]);
      }

      if (handle_redirection(cmd) == -1) {
        exit(EXIT_FAILURE);
      }

      execvp(cmd->argv.data[0], cmd->argv.data);
      perror("execvp");
      exit(EXIT_FAILURE);
    } else if (pid < 0) {
      perror("fork");
      return -1;
    }

    if (in_fd != STDIN_FILENO)
      close(in_fd);
    
    if (cmd->next != NULL) {
      close(pipefd[1]);
      in_fd = pipefd[0];
    }

    if (!cmd->background) {
      waitpid(pid, &status, 0);
      if (WIFEXITED(status)) {
        status = WEXITSTATUS(status);
        if (status != 0 && cmd->and_next)
          return status;
        if (status == 0 && cmd->or_next)
          return status;
      } else {
        fprintf(stderr, "Child process terminated abnormally\n");
        return -1;
      }
    } else {
      printf("[%d] %s\n", pid, cmd->argv.data[0]);
    }

    cmd = cmd->next;
  }

  return status;
}