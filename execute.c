#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include "execute.h"

extern int yyparse(void);
extern void yylex_destroy(void);
extern void yy_scan_string(const char *str);

#define INITIAL_ARGV_SIZE 10
#define ARGV_GROWTH_FACTOR 2
#define MAX_INPUT_LENGTH 4096

extern CommandList* command_list;

static void print_error(const char* message) {
  fprintf(stderr, "Error: %s\n", message);
}

static void* safe_malloc(size_t size) {
  void* ptr = malloc(size);
  if (ptr == NULL) {
    print_error("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  return ptr;
}

static void* safe_realloc(void* ptr, size_t size) {
  void* new_ptr = realloc(ptr, size);
  if (new_ptr == NULL && size != 0) {
    print_error("Memory reallocation failed");
    free(ptr);
    exit(EXIT_FAILURE);
  }
  return new_ptr;
}

static char* safe_strdup(const char* str) {
  if (str == NULL) return NULL;
  char* new_str = strdup(str);
  if (new_str == NULL) {
    print_error("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  return new_str;
}

Command* create_command(void) {
  Command* cmd = safe_malloc(sizeof(Command));
  memset(cmd, 0, sizeof(Command));
  cmd->uid = getuid();
  cmd->gid = getgid();
  cmd->umask = 022;
  return cmd;
}

bool add_argument(Command* cmd, const char* arg) {
  if (cmd == NULL || arg == NULL) return false;
  
  if (cmd->argc == 0 || cmd->argc % INITIAL_ARGV_SIZE == 0) {
    size_t new_size = (cmd->argc + INITIAL_ARGV_SIZE) * sizeof(char*);
    cmd->argv = safe_realloc(cmd->argv, new_size);
  }
  
  cmd->argv[cmd->argc] = safe_strdup(arg);
  cmd->argc++;
  cmd->argv[cmd->argc] = NULL;
  return true;
}

bool add_redirect(Command* cmd, RedirectType type, int fd, const char* target) {
  if (cmd == NULL || target == NULL || cmd->num_redirects >= MAX_REDIRECTS) return false;
  
  Redirect* redirect = &cmd->redirects[cmd->num_redirects];
  redirect->type = type;
  redirect->fd = fd;
  redirect->target = safe_strdup(target);
  redirect->is_fd = (target[0] >= '0' && target[0] <= '9' && target[1] == '\0');
  
  cmd->num_redirects++;
  return true;
}

bool add_pipeline(Command* cmd, Command* next) {
  if (cmd == NULL) return false;
  Command* last = cmd;
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = next;
  return true;
}

static void print_indent(int depth) {
  for (int i = 0; i < depth && i < MAX_INDENT; i++) {
    printf("  ");
  }
}

static void print_redirect(const Redirect* redirect, int depth) {
  print_indent(depth);
  printf("Redirect:\n");
  print_indent(depth + 1);
  printf("type: %d\n", redirect->type);
  print_indent(depth + 1);
  printf("fd: %d\n", redirect->fd);
  print_indent(depth + 1);
  printf("target: '%s'\n", redirect->target);
}

static void print_command(const Command* cmd, int depth) {
  if (cmd == NULL) return;
  
  print_indent(depth);
  printf("Command:\n");
  print_indent(depth + 1);
  printf("argc: %d\n", cmd->argc);
  print_indent(depth + 1);
  printf("argv: ");
  for (int i = 0; i < cmd->argc && cmd->argv[i] != NULL; i++) {
    printf("'%s' ", cmd->argv[i]);
  }
  printf("\n");

  print_indent(depth + 1);
  printf("redirects:\n");
  for (int i = 0; i < cmd->num_redirects; i++) {
    print_redirect(&cmd->redirects[i], depth + 2);
  }

  print_indent(depth + 1);
  printf("background: %d\n", cmd->background);
  print_indent(depth + 1);
  printf("uid: %d\n", cmd->uid);
  print_indent(depth + 1);
  printf("gid: %d\n", cmd->gid);
  print_indent(depth + 1);
  printf("working_directory: %s\n", cmd->working_directory ? cmd->working_directory : "(not set)");
  print_indent(depth + 1);
  printf("umask: %03o\n", cmd->umask);

  print_indent(depth + 1);
  printf("environment variables:\n");
  for (int i = 0; i < cmd->num_env_vars; i++) {
    print_indent(depth + 2);
    printf("%s\n", cmd->environment[i]);
  }

  if (cmd->resource_limits_set) {
    print_indent(depth + 1);
    printf("resource limits:\n");
    for (int i = 0; i < RLIMIT_NLIMITS; i++) {
      print_indent(depth + 2);
      printf("Resource %d: soft=%lu, hard=%lu\n", i,
           (unsigned long)cmd->resource_limits[i].rlim_cur,
           (unsigned long)cmd->resource_limits[i].rlim_max);
    }
  }

  if (cmd->subcommand) {
    print_indent(depth + 1);
    printf("Subcommand:\n");
    print_command_list(cmd->subcommand, depth + 2);
  }

  if (cmd->and_next) {
    print_indent(depth + 1);
    printf("AND next command\n");
  }

  if (cmd->or_next) {
    print_indent(depth + 1);
    printf("OR next command\n");
  }

  if (cmd->next) {
    print_indent(depth + 1);
    printf("Piped to:\n");
    print_command(cmd->next, depth + 2);
  }
}

void print_command_list(const CommandList* list, int depth) {
  if (list == NULL) return;
  
  const Command* cmd = list->head;
  int command_count = 0;
  
  while (cmd != NULL) {
    print_indent(depth);
    printf("Command %d:\n", command_count + 1);
    print_command(cmd, depth + 1);
    
    cmd = cmd->next;
    command_count++;
    
    if (cmd != NULL) {
      printf("\n");
    }
  }
}

bool set_command_user(Command* cmd, uid_t uid, gid_t gid) {
  if (cmd == NULL) return false;
  cmd->uid = uid;
  cmd->gid = gid;
  return true;
}

bool set_command_working_directory(Command* cmd, const char* directory) {
  if (cmd == NULL || directory == NULL) return false;
  
  char* new_dir = strdup(directory);
  if (new_dir == NULL) {
    print_error("Memory allocation failed");
    return false;
  }
  
  free(cmd->working_directory);
  cmd->working_directory = new_dir;
  return true;
}

bool add_environment_variable(Command* cmd, const char* name, const char* value) {
  if (cmd == NULL || name == NULL || value == NULL) return false;
  
  size_t len = strlen(name) + strlen(value) + 2;
  char* env_var = safe_malloc(len);
  
  snprintf(env_var, len, "%s=%s", name, value);
  
  cmd->environment = safe_realloc(cmd->environment, sizeof(char*) * (cmd->num_env_vars + 1));
  cmd->environment[cmd->num_env_vars++] = env_var;
  
  return true;
}

void set_command_umask(Command* cmd, mode_t umask) {
  if (cmd == NULL) return;
  cmd->umask = umask;
}

bool set_resource_limit(Command* cmd, int resource, const struct rlimit* rlim) {
  if (cmd == NULL || rlim == NULL || resource < 0 || resource >= RLIMIT_NLIMITS) return false;
  
  if (!cmd->resource_limits_set) {
    cmd->resource_limits = calloc(RLIMIT_NLIMITS, sizeof(struct rlimit));
    if (cmd->resource_limits == NULL) {
      print_error("Memory allocation failed");
      return false;
    }
    cmd->resource_limits_set = true;
  }
  memcpy(&cmd->resource_limits[resource], rlim, sizeof(struct rlimit));
  return true;
}

CommandList* create_command_list(void) {
  CommandList* list = calloc(1, sizeof(CommandList));
  if (list == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  return list;
}

bool add_command(CommandList* list, Command* cmd) {
  if (list == NULL || cmd == NULL) return false;
  
  if (list->head == NULL) {
    list->head = cmd;
    list->tail = cmd;
  } else {
    list->tail->next = cmd;
    list->tail = cmd;
  }
  return true;
}

bool append_command_list(CommandList* dest, CommandList* src) {
  if (dest == NULL || src == NULL) return false;
  
  if (dest->head == NULL) {
    dest->head = src->head;
    dest->tail = src->tail;
  } else {
    dest->tail->next = src->head;
    dest->tail = src->tail;
  }
  
  src->head = NULL;
  src->tail = NULL;
  return true;
}

void free_command(Command* cmd) {
  if (cmd == NULL) return;
  for (int i = 0; i < cmd->argc; i++)
    free(cmd->argv[i]);
  free(cmd->argv);
  for (int i = 0; i < cmd->num_redirects; i++)
    free(cmd->redirects[i].target);
  free(cmd->working_directory);
  for (int i = 0; i < cmd->num_env_vars; i++)
    free(cmd->environment[i]);
  free(cmd->environment);
  free(cmd->resource_limits);
  if (cmd->subcommand)
    free_command_list(cmd->subcommand);
  memset(cmd, 0, sizeof(Command));
  free(cmd);
}

void free_command_list(CommandList* list) {
  if (list == NULL) return;
  
  Command* current = list->head;
  while (current != NULL) {
    Command* next = current->next;
    free_command(current);
    current = next;
  }
  
  memset(list, 0, sizeof(CommandList));
  free(list);
}

void set_pipeline_background(CommandList* list) {
  if (list == NULL) return;
  Command* cmd = list->head;
  while (cmd != NULL) {
    cmd->background = true;
    cmd = cmd->next;
  }
}

int parse_and_execute(const char* input) {
  if (input == NULL) {
    print_error("Invalid input");
    return -1;
  }
  
  size_t input_len = strlen(input);
  if (input_len > MAX_INPUT_LENGTH) {
    print_error("Input exceeds maximum allowed length");
    return -1;
  }

  char* sanitized_input = safe_malloc(input_len + 1);
  size_t sanitized_len = 0;
  for (size_t i = 0; i < input_len; i++) {
    if (isprint(input[i]) || isspace(input[i]))
      sanitized_input[sanitized_len++] = input[i];
  }
  sanitized_input[sanitized_len] = '\0';

  yy_scan_string(sanitized_input);
  command_list = NULL;
  int result = yyparse();
  yylex_destroy();
  
  free(sanitized_input);
  
  if (result == 0) {
    if (command_list != NULL) {
      printf("Parsed command list:\n");
      print_command_list(command_list, 1);
      free_command_list(command_list);
      command_list = NULL;
    } else {
      printf("No commands to execute\n");
    }
  } else {
    printf("Failed to parse the command\n");
  }
  return result;
}