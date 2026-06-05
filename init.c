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
#include "job.h"

static char* last_cmd = NULL;

/* SIGINT handler for the interactive shell.
 *
 * It is intentionally a no-op. Ctrl-C is delivered to the foreground job's
 * process group (see job control in execute.c/pipeline.c), so the shell itself
 * normally never receives it while a command runs. The handler exists only to
 * give SIGINT a caught (non-default, non-ignored) disposition so that:
 *   1. a stray SIGINT in the brief windows where the shell is the foreground
 *      group outside readline cannot terminate it, and
 *   2. readline installs and later restores its own line-editing SIGINT handler
 *      around input -- it leaves SIG_IGN untouched, so an empty handler rather
 *      than SIG_IGN is required for Ctrl-C to clear the prompt line.
 *
 * It must remain async-signal-safe: no stdio, no malloc/free, and in particular
 * it must NOT touch keep_running (doing so used to make Ctrl-C kill the shell). */
static void handle_sigint(int sig) {
  (void)sig;
}

static int setup_signal_handler(void) {
  if (!shell_is_interactive())
    return 0; /* leave default dispositions for non-interactive use */

  struct sigaction sa = {0};
  sa.sa_handler = handle_sigint;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGINT, &sa, NULL) == -1) {
    perror("Error setting up signal handler");
    return -1;
  }

  /* Ignore job-control signals so that tcsetpgrp() never stops the shell when
   * it hands the terminal back and forth, and so Ctrl-\ / Ctrl-Z at the prompt
   * neither kill nor suspend it. Forked children restore SIGINT/SIGQUIT to the
   * default before exec via reset_child_signals(). */
  struct sigaction ign = {0};
  ign.sa_handler = SIG_IGN;
  sigemptyset(&ign.sa_mask);
  ign.sa_flags = 0;
  sigaction(SIGQUIT, &ign, NULL);
  sigaction(SIGTSTP, &ign, NULL);
  sigaction(SIGTTIN, &ign, NULL);
  sigaction(SIGTTOU, &ign, NULL);
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
  /* Establish job control (process group + terminal ownership) before touching
   * signal dispositions: init_shell() relies on the default SIGTTIN action. */
  init_shell();
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