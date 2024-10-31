#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "builtin.h"
#include "variable.h"
#include "io.h"
#include "rstring.h"
#include "strconv.h"

extern volatile sig_atomic_t keep_running;
extern volatile int last_status;

int builtin_exit(Command *cmd) {
  last_status = 0;
  if (cmd->argv.size > 1) {
    int status = -997;
    string code = *(string*)array_checked_get(cmd->argv, 1);
    StrconvResult result = ratoi(code, &status);
    if (result.is_err) ffprintln(stderr, "exit: %S: numeric argument required", code);
    last_status = status;
  }
  println(_SLIT("exit"));
  keep_running = 0;
  return 0;
}