#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
#include <locale.h>
#include <readline/readline.h>
#include "rick.h"
#include "result.h"
#include "error.h"
#include "expr.h"
#include "execute.h"
#include "parser.tab.h"
#include "color.h"
#include "memory.h"
#include "file.h"
#include "log.h"
#include "job.h"
#include "variable.h"
#include "io.h"

#define INITIAL_BUFFER_SIZE (1 * 1024)  // 1 KB initial size
#define MAX_BUFFER_SIZE (100 * 1024 * 1024)  // 100 MB limit
#define MAX_COMMAND_LENGTH 1000  // Maximum length for a single command
#define BUFFER_GROWTH_FACTOR 2

extern int yylex_destroy(void);

volatile sig_atomic_t keep_running = 1;
volatile int last_status = 0;

static IntResult process_command(const string input) {
  NTRY(parse_and_execute(input, (int*)&last_status));
  return Ok(NULL);
}

int main(void) {
  init_rickshell();

  println(_SLIT("Enter commands (type 'exit' to quit):"));
  log_info("Shell started");

  while (keep_running) {
    print_job_status();
    string input = get_input();

    if (string__is_null_or_empty(input)) {
      string__free(input);
      if (feof(stdin)) {
        println(_SLIT0);
        println(_SLIT("exit"));
        break;
      }
      continue;
    }

    if (input.len == 0) {
      string__free(input);
      continue;
    }

    println(_SLIT0);
    Result result = process_command(input);
    string__free(input);

    if (result.is_err) {
      report_error(result);
    }

    yylex_destroy();
  }
  
  cleanup_rickshell();

  return last_status;
}