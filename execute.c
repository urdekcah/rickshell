#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "expr.h"
#include "pipeline.h"
#include "redirect.h"
#include "execute.h"
#include "memory.h"
#include "error.h"

extern int yyparse(void);
extern int yylex_destroy(void);
extern void yy_scan_string(const char *str);

#define MAX_INPUT_LENGTH 4096

extern CommandList* command_list;

int execute_command(Command* cmd) {
  pid_t pid;
  int status;

  pid = fork();
  if (pid == 0) {
    if (handle_redirection(cmd) == -1) {
      exit(1);
    }

    execvp(cmd->argv.data[0], cmd->argv.data);
    perror("execvp failed");
    exit(127);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      fprintf(stderr, "Command terminated by signal %d\n", WTERMSIG(status));
      return 128 + WTERMSIG(status);
    }
  } else {
    perror("fork failed");
    return -1;
  }

  return -1;
}

char* command_to_string(Command* cmd) {
  if (cmd == NULL) return NULL;

  size_t total_length = 0;
  for (int i = 0; cmd->argv.data[i] != NULL; i++) {
    total_length += strlen(cmd->argv.data[i]) + 1;
  }

  for (size_t i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    total_length += 3;
    total_length += strlen(redir->target);
  }

  char* result = (char*)rmalloc(total_length + 1);
  if (result == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  result[0] = '\0';

  for (int i = 0; cmd->argv.data[i] != NULL; i++) {
    strcat(result, cmd->argv.data[i]);
    if (cmd->argv.data[i + 1] != NULL) {
      strcat(result, " ");
    }
  }

  for (size_t i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    switch (redir->type) {
      case REDIRECT_INPUT:
        strcat(result, " < ");
        break;
      case REDIRECT_OUTPUT:
        strcat(result, " > ");
        break;
      case REDIRECT_APPEND:
        strcat(result, " >> ");
        break;
      case REDIRECT_INPUT_DUP:
        strcat(result, " <&");
        break;
      case REDIRECT_OUTPUT_DUP:
        strcat(result, " >&");
        break;
      case REDIRECT_APPEND_DUP:
        strcat(result, " >>&");
        break;
    }
    strcat(result, redir->target);
  }

  if (cmd->background) {
    strcat(result, " &");
  }

  return result;
}

char* command_list_to_string(CommandList* list) {
  if (list == NULL || list->head == NULL) {
    return NULL;
  }

  size_t total_length = 0;
  Command* cmd = list->head;

  while (cmd != NULL) {
    char* cmd_str = command_to_string(cmd);
    if (cmd_str == NULL) {
      return NULL;
    }
    total_length += strlen(cmd_str);
    free(cmd_str);

    if (cmd->next != NULL)
      total_length += (cmd->and_next || cmd->or_next)? 4 : (cmd->semi_next)? 2 : 3;
    cmd = cmd->next;
  }

  char* result = (char*)rmalloc(total_length + 1);
  if (result == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  result[0] = '\0';

  cmd = list->head;
  while (cmd != NULL) {
    char* cmd_str = command_to_string(cmd);
    if (cmd_str == NULL) {
      free(result);
      return NULL;
    }
    strcat(result, cmd_str);
    free(cmd_str);

    if (cmd->next != NULL) {
      if (cmd->and_next) {
        strcat(result, " && ");
      } else if (cmd->or_next) {
        strcat(result, " || ");
      } else if (cmd->semi_next) {
        strcat(result, "; ");
      } else {
        strcat(result, " | ");
      }
    }
    cmd = cmd->next;
  }

  return result;
}

int execute_command_list(CommandList* list) {
  int status = 0;
  Command* cmd = list->head;

  bool background = false;
  Command* last_cmd = list->tail;
  if (last_cmd && last_cmd->background) {
    background = true;
    last_cmd->background = false;
  }

  if (background) {
    char* command_line = command_list_to_string(list);
    return execute_background_job(list, command_line);
  }

  while (cmd != NULL) {
    if (cmd->pipline_next) {
      status = execute_pipeline(cmd);
      while (cmd != NULL && cmd->pipline_next)
        cmd = cmd->next;
    } else {
      status = execute_command(cmd);
      if (status != 0 && cmd->and_next)
        break;
    }
    cmd = cmd->next;
  }
  
  return status;
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

  char* sanitized_input = rmalloc(input_len + 1);
  size_t sanitized_len = 0;
  for (size_t i = 0; i < input_len; i++) {
    if (isprint(input[i]) || isspace(input[i]))
      sanitized_input[sanitized_len++] = input[i];
  }
  sanitized_input[sanitized_len] = '\0';

  char* saveptr;
  char* cmd = strtok_r(sanitized_input, ";", &saveptr);
  int result = 0;

  while (cmd != NULL) {
    while (isspace(*cmd)) cmd++;
    if (*cmd != '\0') {
      yy_scan_string(cmd);
      command_list = NULL;
      result = yyparse();
      yylex_destroy();
      
      if (result == 0) {
        if (command_list != NULL) {
          bool background = false;
          Command* last_cmd = command_list->tail;
          if (last_cmd && last_cmd->background) {
            background = true;
            last_cmd->background = false;
          }

          if (background) {
            char* command_line = command_list_to_string(command_list);
            execute_background_job(command_list, command_line);
            free(command_line);
          } else {
            int status = execute_command_list(command_list);
            (void)status;
          }
          free_command_list(command_list);
          command_list = NULL;
        } else {
          printf("No commands to execute\n");
        }
      } else {
        printf("Failed to parse the command\n");
        break;
      }
    }
    cmd = strtok_r(NULL, ";", &saveptr);
  }
  
  free(sanitized_input);
  return result;
}