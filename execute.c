#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdbool.h>
#include "execute.h"

extern CommandList* command_list;

Command* create_command() {
  Command* cmd = malloc(sizeof(Command));
  if (cmd == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return NULL;
  }
  memset(cmd, 0, sizeof(Command));
  cmd->uid = getuid();
  cmd->gid = getgid();
  cmd->umask = 022;
  return cmd;
}

void add_argument(Command* cmd, char* arg) {
  if (cmd == NULL || arg == NULL) return;
  
  cmd->argc++;
  cmd->argv = realloc(cmd->argv, sizeof(char*) * (cmd->argc + 1));
  if (cmd->argv == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return;
  }
  cmd->argv[cmd->argc - 1] = strdup(arg);
  cmd->argv[cmd->argc] = NULL;
}

void add_pipeline(Command* cmd, Command* next) {
  if (cmd == NULL) return;
  Command* last = cmd;
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = next;
}

void print_command(Command* cmd, int depth) {
  if (cmd == NULL) return;
  
  char indent[64] = "";
  for (int i = 0; i < depth; i++) {
    strcat(indent, "  ");
  }
  
  printf("%sCommand:\n", indent);
  printf("%s  argc: %d\n", indent, cmd->argc);
  printf("%s  argv: ", indent);
  for (int i = 0; i < cmd->argc && cmd->argv[i] != NULL; i++) {
    printf("'%s' ", cmd->argv[i]);
  }
  printf("\n");

  printf("%s  redirects:\n", indent);
  for (int i = 0; i < cmd->num_redirects; i++) {
    printf("%s  Redirect %d:\n", indent, i + 1);
    printf("%s    type: %d\n", indent, cmd->redirects[i].type);
    printf("%s    fd: %d\n", indent, cmd->redirects[i].fd);
    printf("%s    target: '%s'\n", indent, cmd->redirects[i].target);
  }

  printf("%s  background: %d\n", indent, cmd->background);
  printf("%s  uid: %d\n", indent, cmd->uid);
  printf("%s  gid: %d\n", indent, cmd->gid);
  printf("%s  working_directory: %s\n", indent, cmd->working_directory ? cmd->working_directory : "(not set)");
  printf("%s  umask: %03o\n", indent, cmd->umask);

  printf("%s  environment variables:\n", indent);
  for (int i = 0; i < cmd->num_env_vars; i++) {
    printf("%s  %s\n", indent, cmd->environment[i]);
  }

  if (cmd->resource_limits_set) {
    printf("%s  resource limits:\n", indent);
    for (int i = 0; i < RLIMIT_NLIMITS; i++) {
      printf("%s  Resource %d: soft=%lu, hard=%lu\n", indent, i,
           (unsigned long)cmd->resource_limits[i].rlim_cur,
           (unsigned long)cmd->resource_limits[i].rlim_max);
    }
  }

  if (cmd->subcommand) {
    printf("%s  Subcommand:\n", indent);
    print_command_list(cmd->subcommand, depth + 1);
  }

  if (cmd->and_next) {
    printf("%s  AND next command\n", indent);
  }

  if (cmd->or_next) {
    printf("%s  OR next command\n", indent);
  }

  if (cmd->next) {
    printf("%s  Piped to:\n", indent);
    print_command(cmd->next, depth + 1);
  }
}

void print_command_list(CommandList* list, int depth) {
  if (list == NULL) return;
  
  Command* cmd = list->head;
  int command_count = 0;
  
  while (cmd != NULL) {
    char indent[64] = "";
    for (int i = 0; i < depth; i++) {
      strcat(indent, "  ");
    }
    printf("%sCommand %d:\n", indent, command_count + 1);
    print_command(cmd, depth + 1);
    
    cmd = cmd->next;
    command_count++;
    
    if (cmd != NULL) {
      printf("\n");
    }
  }
}

void set_command_user(Command* cmd, uid_t uid, gid_t gid) {
  if (cmd == NULL) return;
  cmd->uid = uid;
  cmd->gid = gid;
}

void set_command_working_directory(Command* cmd, const char* directory) {
  if (cmd == NULL || directory == NULL) return;
  
  free(cmd->working_directory);
  cmd->working_directory = strdup(directory);
}

void add_environment_variable(Command* cmd, const char* name, const char* value) {
  if (cmd == NULL || name == NULL || value == NULL) return;
  
  cmd->num_env_vars++;
  cmd->environment = realloc(cmd->environment, sizeof(char*) * cmd->num_env_vars);
  if (cmd->environment == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return;
  }
  int len = snprintf(NULL, 0, "%s=%s", name, value) + 1;
  cmd->environment[cmd->num_env_vars - 1] = malloc(len);
  if (cmd->environment[cmd->num_env_vars - 1] == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return;
  }
  snprintf(cmd->environment[cmd->num_env_vars - 1], len, "%s=%s", name, value);
}

void set_command_umask(Command* cmd, int umask) {
  if (cmd == NULL) return;
  cmd->umask = umask;
}

void set_resource_limit(Command* cmd, int resource, const struct rlimit* rlim) {
  if (cmd == NULL || rlim == NULL || resource < 0 || resource >= RLIMIT_NLIMITS) return;
  
  if (!cmd->resource_limits_set) {
    cmd->resource_limits = malloc(sizeof(struct rlimit) * RLIMIT_NLIMITS);
    if (cmd->resource_limits == NULL) {
      fprintf(stderr, "Error: Memory allocation failed\n");
      return;
    }
    cmd->resource_limits_set = 1;
  }
  memcpy(&cmd->resource_limits[resource], rlim, sizeof(struct rlimit));
}

void append_command_list(CommandList* dest, CommandList* src) {
  if (dest == NULL || src == NULL) return;
  
  if (dest->head == NULL) {
    dest->head = src->head;
    dest->tail = src->tail;
  } else {
    dest->tail->next = src->head;
    dest->tail = src->tail;
  }
  
  src->head = NULL;
  src->tail = NULL;
}

CommandList* create_command_list() {
  CommandList* list = malloc(sizeof(CommandList));
  if (list == NULL) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    return NULL;
  }
  list->head = NULL;
  list->tail = NULL;
  return list;
}

void add_command(CommandList* list, Command* cmd) {
  if (list == NULL || cmd == NULL) return;
  
  if (list->head == NULL) {
    list->head = cmd;
    list->tail = cmd;
  } else {
    list->tail->next = cmd;
    list->tail = cmd;
  }
}

void add_redirect(Command* cmd, RedirectType type, int fd, char* target) {
  if (cmd == NULL || target == NULL || cmd->num_redirects >= MAX_REDIRECTS) return;
  
  Redirect* redirect = &cmd->redirects[cmd->num_redirects];
  redirect->type = type;
  redirect->fd = fd;
  redirect->target = strdup(target);
  redirect->is_fd = (target[0] >= '0' && target[0] <= '9' && target[1] == '\0');
  
  cmd->num_redirects++;
}

void free_command(Command* cmd) {
  if (cmd == NULL) return;
  
  for (int i = 0; i < cmd->argc; i++) {
    free(cmd->argv[i]);
  }
  free(cmd->argv);
  
  for (int i = 0; i < cmd->num_redirects; i++) {
    free(cmd->redirects[i].target);
  }
  
  free(cmd->working_directory);
  
  for (int i = 0; i < cmd->num_env_vars; i++) {
    free(cmd->environment[i]);
  }
  free(cmd->environment);
  
  free(cmd->resource_limits);
  
  if (cmd->subcommand) {
    free_command_list(cmd->subcommand);
  }
  
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
  
  free(list);
}

void set_pipeline_background(CommandList* list) {
  if (list == NULL) return;
  Command* cmd = list->head;
  while (cmd != NULL) {
    cmd->background = 1;
    cmd = cmd->next;
  }
}

int parse_and_execute(char* input) {
  yy_scan_string(input);
  command_list = NULL;
  int result = yyparse();
  yylex_destroy();
  
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