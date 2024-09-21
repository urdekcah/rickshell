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

#define INITIAL_BUFFER_SIZE (1 * 1024)  // 1 KB initial size
#define MAX_BUFFER_SIZE (100 * 1024 * 1024)  // 100 MB limit
#define MAX_COMMAND_LENGTH 1000  // Maximum length for a single command
#define BUFFER_GROWTH_FACTOR 1.5

extern int yylex_destroy(void);

static volatile sig_atomic_t keep_running = 1;

static void handle_sigint(int sig) {
  (void)sig;  // Unused parameter 
  keep_running = 0;
  const char msg[] = "\nUse 'exit' to quit the shell.\n> ";
  if (write(STDOUT_FILENO, msg, sizeof(msg) - 1) == -1) {
    _exit(EXIT_FAILURE);
  }
}

static char* get_hostname(void) {
  char* hostname = NULL;
  long host_name_max = sysconf(_SC_HOST_NAME_MAX);
  if (host_name_max == -1)
    host_name_max = _POSIX_HOST_NAME_MAX;
  hostname = rmalloc(host_name_max + 1);
  if (hostname == NULL) {
    perror("Error allocating memory");
    return NULL;
  }

  if (gethostname(hostname, host_name_max + 1) != 0) {
    perror("Error getting hostname");
    free(hostname);
    return NULL;
  }

  char* copy = strdup(hostname);
  free(hostname);
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
    printf(ANSI_COLOR_BOLD_BLUE "%s@%s" ANSI_COLOR_BOLD_BLACK ":" ANSI_COLOR_BOLD_YELLOW "%s" ANSI_COLOR_RESET "$ ", username, hostname, cwd);
  } else {
    printf("daniel@fishydino:~$ ");  // Daniel (Elliott): English name of rickroot30
  }

  fflush(stdout);

  free(hostname);
  free(username);
  free(cwd);
}

static char* get_input(size_t* size) {
  char* buffer = rmalloc(INITIAL_BUFFER_SIZE);
  if (!buffer) {
    perror("Error allocating memory");
    return NULL;
  }

  size_t buffer_size = INITIAL_BUFFER_SIZE;
  size_t input_length = 0;

  while (1) {
    size_t remaining = buffer_size - input_length;
    if (!fgets(buffer + input_length, remaining, stdin)) {
      if (feof(stdin)) {
        if (input_length == 0) {
          free(buffer);
          return NULL;
        }
        break;
      } else {
        perror("Error reading input");
        free(buffer);
        return NULL;
      }
    }

    input_length += strlen(buffer + input_length);
    if (input_length > 0 && buffer[input_length - 1] == '\n') {
      buffer[--input_length] = '\0';
      break;
    }

    if (input_length == buffer_size - 1) {
      if (buffer_size >= MAX_BUFFER_SIZE) {
        fprintf(stderr, "Error: Input too large\n");
        free(buffer);
        return NULL;
      }
      size_t new_size = (size_t)(buffer_size * BUFFER_GROWTH_FACTOR);
      if (new_size > MAX_BUFFER_SIZE) {
        new_size = MAX_BUFFER_SIZE;
      }
      char* new_buffer = realloc(buffer, new_size);
      if (!new_buffer) {
        perror("Error reallocating memory");
        free(buffer);
        return NULL;
      }
      buffer = new_buffer;
      buffer_size = new_size;
    }
  }

  *size = input_length;
  return buffer;
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

static int process_command(const char* input) {
  if (strlen(input) > MAX_COMMAND_LENGTH) {
    fprintf(stderr, "Error: Command too long\n");
    return -1;
  }

  if (strcmp(input, "exit") == 0) return 1;
  parse_and_execute((char*)input);
  return 0;
}

int main(void) {
  if (setup_signal_handler() == -1) {
    return EXIT_FAILURE;
  }

  printf("Enter commands (type 'exit' to quit):\n");

  while (keep_running) {
    print_job_status();
    print_prompt();

    size_t input_size;
    char* input = get_input(&input_size);

    if (!input) {
      if (feof(stdin)) {
        printf("\nEnd of input. Exiting...\n");
        break;
      }
      continue;
    }

    if (input_size == 0) {
      free(input);
      continue;
    }

    int result = process_command(input);
    free(input);

    if (result == 1) {
      printf("Exiting...\n");
      break;
    }

    yylex_destroy();
  }

  return EXIT_SUCCESS;
}