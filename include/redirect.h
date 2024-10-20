#ifndef __RICKSHELL_REDIRECT_H__
#define __RICKSHELL_REDIRECT_H__
#include <stdbool.h>
#include "expr.h"
#include "result.h"

enum RedirectType {
  REDIRECT_INPUT,
  REDIRECT_OUTPUT,
  REDIRECT_APPEND,
  REDIRECT_INPUT_DUP,
  REDIRECT_OUTPUT_DUP,
  REDIRECT_APPEND_DUP
};

struct Redirect {
  RedirectType type;
  int fd;
  string target;
  bool is_fd;
};

IntResult handle_redirection(Command* cmd);
#endif /* __RICKSHELL_REDIRECT_H__ */