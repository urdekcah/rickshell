#define _XOPEN_SOURCE 700
#include <locale.h>
#include <signal.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "variable.h"
#include "log.h"
#include "file.h"
#include "io.h"
#include "memory.h"
#include "history.h"

extern volatile sig_atomic_t keep_running;
static char* last_cmd = NULL;

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

void initialize_history() {
  char* path = expand_home_directory(DEFAULT_HISTFILE);
  read_history(path);
  rfree(path);
}

char* get_last_command() {
  if (history_length == 0)
    return NULL;

  HIST_ENTRY* last_entry = history_get(history_length);
  if (last_entry == NULL)
    return NULL;

  return strdup(last_entry->line);
}

void reset_last_command() {
  rfree(last_cmd);
  last_cmd = get_last_command();
}

void save_history(char* cmd) {
  if (!cmd || (last_cmd && strcmp(cmd, last_cmd) == 0)) return;
  char* path = expand_home_directory(DEFAULT_HISTFILE);
  write_history(path);
  rfree(path);
  rfree(last_cmd);
  last_cmd = rstrdup(cmd);
  add_history(cmd);
}

void init_rickshell() {
  setlocale(LC_ALL, "");
  if (setup_signal_handler() == -1) _exit(1);
  ensure_directory_exist("~/.rickshell");
  parse_path();
  rl_redisplay_function = rick__redisplay_function;
  init_variables();
  initialize_history();
  last_cmd = get_last_command();
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
}

void cleanup_rickshell() {
  cleanup_variables();
  log_info("Shell exited");
  log_shutdown();
  rfree(last_cmd);
  rl_clear_history();
  rl_cleanup_after_signal();
}