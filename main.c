#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <pwd.h>
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

static char* get_hostname(void) {
  char* hostname = NULL;
  long host_name_max = sysconf(_SC_HOST_NAME_MAX);
  if (host_name_max == -1)
    host_name_max = _POSIX_HOST_NAME_MAX;
  hostname = rmalloc((size_t)host_name_max + 1);
  if (hostname == NULL) {
    perror("Error allocating memory");
    return NULL;
  }

  if (gethostname(hostname, (size_t)host_name_max + 1) != 0) {
    perror("Error getting hostname");
    rfree(hostname);
    return NULL;
  }

  char* copy = strdup(hostname);
  rfree(hostname);
  return copy;
}

static char* get_username(void) {
  struct passwd *pw = getpwuid(getuid());
  if (pw == NULL) {
    perror("Failed to get username");
    return NULL;
  }

  char* username = strdup(pw->pw_name);
  if (username == NULL) {
    perror("Failed to allocate memory for username");
    return NULL;
  }

  return username;
}

static char* get_current_directory() {
  char* cwd = getcwd(NULL, 0);
  if (cwd == NULL) {
    perror("Error getting current directory");
    return NULL;
  }

  return cwd;
}

static void print_prompt(void) {
  char* hostname = get_hostname();
  char* username = get_username();
  char* cwd = get_current_directory();

  if (hostname && username && cwd) {
    fprint(ANSI_COLOR_BOLD_BLUE "%s@%s" ANSI_COLOR_BOLD_BLACK ":" ANSI_COLOR_BOLD_YELLOW "%s" ANSI_COLOR_RESET "$ ", username, hostname, cwd);
  } else {
    fprint("daniel@fishydino:~$ ");  // Daniel (Elliott): English name of rickroot30
  }

  fflush(stdout);

  rfree(hostname);
  rfree(username);
  rfree(cwd);
}

static void handle_sigint(int sig) {
  (void)sig;
  keep_running = 0;
  println(_SLIT0);
  print_prompt();
}

static string get_input() {
  StringBuilder sb = string_builder__new();
  char buffer[INITIAL_BUFFER_SIZE];
    
  while (1) {
    if (!fgets(buffer, sizeof(buffer), stdin)) {
      if (feof(stdin)) {
        if (sb.len == 0) {
          string_builder__free(&sb);
          return _SLIT0;
        }
        break;
      } else {
        perror("Error reading input");
        string_builder__free(&sb);
        return _SLIT0;
      }
    }
          
    size_t len = strlen(buffer);
    if (len > 0 && buffer[len - 1] == '\n') {
      buffer[len - 1] = '\0';
      len--;
    }

    string tv = string__new(buffer);
    string_builder__append(&sb, tv);
    string__free(tv);
        
    if (len < sizeof(buffer) - 1) {
      break;
    }
        
    if (sb.len >= MAX_BUFFER_SIZE) {
      ffprintln(stderr, "Error: Input too large");
      string_builder__free(&sb);
      return _SLIT0;
    }
  }
  
  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
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

static int process_command(const string input) {
  parse_and_execute(input);
  return 0;
}

int main(void) {
  ensure_directory_exist("~/.rickshell");
  if (setup_signal_handler() == -1) {
    return EXIT_FAILURE;
  }

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
    print_prompt();

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

    int result = process_command(input);
    string__free(input);

    if (result == 1) {
      println(_SLIT("Exiting..."));
      break;
    }

    yylex_destroy();
  }
  cleanup_variables();
  log_info("Shell exited");
  log_shutdown();

  return last_status;
}