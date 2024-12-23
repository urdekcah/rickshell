#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pwd.h>
#include <readline/readline.h>
#include "color.h"
#include "error.h"
#include "memory.h"
#include "result.h"
#include "rstring.h"
#include "builtin.h"
#include "file.h"
#include "io.h"

#define INITIAL_BUFFER_SIZE 256
#define CTRL_KEY(k) ((k) & 0x1f)

extern char *path_dirs[MAX_PATH_DIRS];
extern int path_dir_count;
string prompt = _SLIT0;
size_t prompt_len = 0;

static int is_command_char(char c) {
  return isalnum(c) || c == '_' || c == '-' || c == '.';
}

static int command_exists(const string cmd) {
  if (get_builtin_func(cmd) != NULL) {
    return 1;
  }

  StringBuilder s = string_builder__new();
  struct stat st;

  for (int i = 0; i < path_dir_count; i++) {
    string_builder__append_cstr(&s, path_dirs[i]);
    string_builder__append_char(&s, '/');
    string_builder__append(&s, cmd);
    string full_path = string_builder__to_string(&s);
    string_builder__free(&s);

    if (stat(full_path.str, &st) == 0 && (st.st_mode & S_IXUSR)) {
      string__free(full_path);
      string_builder__free(&s);
      return 1;
    }
    string__free(full_path);
    string_builder__free(&s);
    s = string_builder__new();
  }
  string_builder__free(&s);

  return 0;
}

void rick__redisplay_function(void) {
  char *line = rl_line_buffer;
  int cursor = rl_point;
  size_t len = strlen(line);
    
  char *cmd = NULL;
  size_t cmd_len = 0;
  while (cmd_len < len && is_command_char(line[cmd_len]))
    cmd_len++;
    
  if (cmd_len > 0)
    cmd = strndup(line, cmd_len);

  printf("\r\033[K%s", prompt.str);
  
  if (cmd) {
    string scmd = string__new(cmd);
    const char* color = command_exists(scmd) ? ANSI_COLOR_GREEN : ANSI_COLOR_RED;
    printf("%s%s%s", color, cmd, ANSI_COLOR_RESET);
    free(cmd);
    string__free(scmd);
  }
  
  if (cmd_len < len)
    printf("%s", line + cmd_len);
  
  printf("\r");
  for (size_t i = 0; i < prompt_len + (size_t)cursor; i++)
    printf("\033[C");
  fflush(stdout);
}

string get_input(void) {
  prompt = get_prompt();
  char* raw_input = readline(prompt.str);
  string input = string__new(raw_input);
  free(raw_input);
  string__free(prompt);
  prompt = _SLIT0;
  return input;
}

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

  const char* home = getenv("HOME");
  if (home != NULL && strncmp(cwd, home, strlen(home)) == 0) {
    size_t home_len = strlen(home);
    size_t cwd_len = strlen(cwd);
    char* result = malloc(cwd_len - home_len + 2); // +2 for '~' and '\0'

    if (result == NULL) {
      free(cwd);
      return NULL;
    }
      
    result[0] = '~';
    strcpy(result + 1, cwd + home_len);
    free(cwd);
    return result;
  }

  return cwd;
}

string get_prompt(void) {
  char* hostname = get_hostname();
  char* username = get_username();
  char* cwd = get_current_directory();

  prompt_len = 0;
  StringBuilder s = string_builder__new();
  string_builder__append_cstr(&s, ANSI_COLOR_BOLD_BLUE);
  string_builder__append_cstr(&s, (username != NULL) ? username : "daniel");
  string_builder__append_char(&s, '@');
  string_builder__append_cstr(&s, (hostname != NULL) ? hostname : "fishydino");
  string_builder__append_cstr(&s, ANSI_COLOR_BOLD_BLACK);
  string_builder__append_char(&s, ':');
  string_builder__append_cstr(&s, ANSI_COLOR_BOLD_YELLOW);
  string_builder__append_cstr(&s, (cwd != NULL) ? cwd : "~");
  string_builder__append_cstr(&s, ANSI_COLOR_RESET);
  string_builder__append_cstr(&s, "$ ");
  
  prompt_len += strlen((username != NULL) ? username : "daniel");
  prompt_len += strlen((hostname != NULL) ? hostname : "fishydino");
  prompt_len += strlen((cwd != NULL) ? cwd : "~");
  prompt_len += 4;

  rfree(hostname);
  rfree(username);
  rfree(cwd);
  
  string result = string_builder__to_string(&s);
  string_builder__free(&s);
  return result;
}