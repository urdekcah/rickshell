#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include "expr.h"
#include "redirect.h"
#include "rstring.h"
#include "error.h"
#include "result.h"

IntResult handle_redirection(Command* cmd) {
  register size_t i;
  for (i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    int flags, fd;

    switch (redir->type) {
      case REDIRECT_INPUT:
        fd = open(redir->target.str, O_RDONLY);
        if (fd == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        if (dup2(fd, STDIN_FILENO) == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          close(fd);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        close(fd);
        break;
      case REDIRECT_OUTPUT:
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        fd = open(redir->target.str, flags, 0644);
        if (fd == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          close(fd);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        close(fd);
        break;
      case REDIRECT_APPEND:
        flags = O_WRONLY | O_CREAT | O_APPEND;
        fd = open(redir->target.str, flags, 0644);
        if (fd == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        if (dup2(fd, STDOUT_FILENO) == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          close(fd);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        close(fd);
        break;
      case REDIRECT_INPUT_DUP:
      case REDIRECT_OUTPUT_DUP:
      case REDIRECT_APPEND_DUP:
        fd = atoi(redir->target.str);
        if (dup2(fd, redir->fd) == -1) {
          char* msg = strerror(errno);
          string emsg = string__new(msg);
          return Err(emsg, ERRCODE_EXEC_REDIRECT_FAILED);
        }
        break;
    }
  }
  return Ok(NULL);
}