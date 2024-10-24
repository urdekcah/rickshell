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

static void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
  println(_SLIT0);
  string prompt = get_prompt();
  print(prompt);
  string__free(prompt);
}

static int setup_signal_handler(void) {
  struct sigaction sa = {0};
  sa.sa_handler = handle_sigint;
  sa.sa_flags = SA_RESTART;
  
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("Error setting up signal handler");
    return -1;
  }
  return 0;
}

static IntResult process_command(const string input) {
  NTRY(parse_and_execute(input, (int*)&last_status));
  return Ok(NULL);
}

int main(void) {
  setlocale(LC_ALL, "");
  ensure_directory_exist("~/.rickshell");
  parse_path();
  if (setup_signal_handler() == -1) {
    return EXIT_FAILURE;
  }

  rl_redisplay_function = rick__redisplay_function;

  init_variables();
  LogConfig config = {
    .name = _SLIT("rickshell"),
    .level = LOG_LEVEL_INFO,
    .color_output = true,
    .filename = _SLIT("~/.rickshell/rickshell.log"),
    .max_file_size = 10 * 1024 * 1024,  // 10 MB
    .max_backup_files = 10,
    .append_mode = true,
    .file_output_only = true,
    .log_format = _SLIT("[%Y-%M-%d %H:%M:%S] [%L] [%p] (%a) %f:%l (%n): %m")
  };
  log_init(&config);

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
  cleanup_variables();
  log_info("Shell exited");
  log_shutdown();

  return last_status;
}