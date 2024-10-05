#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <errno.h>
#include "expr.h"
#include "builtin.h"
#include "pipeline.h"
#include "redirect.h"
#include "execute.h"
#include "job.h"
#include "memory.h"
#include "error.h"
#include "variable.h"
#include "strconv.h"

extern int yyparse(void);
extern int yylex_destroy(void);
extern void yy_scan_string(const char *str);

#define MAX_INPUT_LENGTH 4096
#define MAX_COMMAND_LENGTH 1024

extern CommandList* command_list;
extern VariableTable* variable_table;

int execute_command(Command* cmd) {
  if (cmd == NULL || cmd->argv.data == NULL || cmd->argv.data[0] == NULL) {
    print_error("Invalid command");
    return -1;
  }

  for (int i = 0; cmd->argv.data[i] != NULL; i++) {
    char* expanded = expand_variables(variable_table, cmd->argv.data[i]);
    rfree(cmd->argv.data[i]);
    cmd->argv.data[i] = expanded;
  }

  bool should_not_expand = do_not_expand_this_builtin(cmd->argv.data[0]);
  char* equals_sign = strchr(cmd->argv.data[0], '=');
  char* open_bracket = strchr(cmd->argv.data[0], '[');
  char* close_bracket = strrchr(cmd->argv.data[0], ']');
  if (equals_sign != NULL && !should_not_expand) {
    *equals_sign = '\0';
    const char* name = cmd->argv.data[0];
    const char* value = equals_sign + 1;
    
    if (*value == '\0' && cmd->argv.data[1] != NULL)
      value = cmd->argv.data[1];
    
    if (value[0] == '(' && value[strlen(value) - 1] == ')') {
      parse_and_set_array(variable_table, name, value);
    } if (value[0] == '{' && value[strlen(value) - 1] == '}') {
      parse_and_set_associative_array(variable_table, name, value);
    } else if (open_bracket && close_bracket && open_bracket < close_bracket) {
      *open_bracket = '\0';
      *close_bracket = '\0';
      char* key = open_bracket + 1;
      
      Variable* var = get_variable(variable_table, name);
      if (var != NULL && var->type == VAR_ASSOCIATIVE_ARRAY) {
        set_associative_array_variable(variable_table, name, key, value);
      } else if (var != NULL && var->type == VAR_ARRAY) {
        long long index;
        StrconvResult result = ratoll(key, &index);
        if (result.is_err) {
          print_error("Invalid key for associative array");
          return -1;
        }
        array_set_element(variable_table, name, (size_t)index, value);
      } else {
        print_error("Variable is not an array or associative array");
        return -1;
      }
    } else {
      Variable* var = set_variable(variable_table, name, value, parse_variable_type(value), false);
      if (var == NULL) {
        print_error("Failed to set variable");
        return -1;
      }
    }
    *equals_sign = '=';
    return 0;
  } else if (should_not_expand) {
    char** new_data = rcalloc(cmd->argv.capacity, sizeof(char*));
    size_t new_size = 0;
    for (int i = 0; cmd->argv.data[i] != NULL; i++) {
      size_t len = strlen(cmd->argv.data[i]);
      if (len > 0 && cmd->argv.data[i][len - 1] == '=') {
        if (cmd->argv.data[i + 1] != NULL) {
          size_t new_len = len + strlen(cmd->argv.data[i + 1]) + 1;
          new_data[new_size] = rcalloc(new_len, sizeof(char));
          snprintf(new_data[new_size], new_len, "%s%s", cmd->argv.data[i], cmd->argv.data[i + 1]);
          i++;
        } else {
          new_data[new_size] = rstrdup(cmd->argv.data[i]);
        }
        new_size++;
      } else {
        new_data[new_size] = rstrdup(cmd->argv.data[i]);
        new_size++;
      }
    }
    for (int i = 0; cmd->argv.data[i] != NULL; i++)
      rfree(cmd->argv.data[i]);
    rfree(cmd->argv.data);
    cmd->argv.data = new_data;
    cmd->argv.size = new_size;
  }

  int builtin_result = execute_builtin(cmd);
  if (builtin_result != -1)
    return builtin_result;

  pid_t pid = fork();
  if (pid == -1) {
    print_error("Fork failed");
    return -1;
  } else if (pid == 0) {
    if (handle_redirection(cmd) == -1) {
      _exit(EXIT_FAILURE);
    }

    execvp(cmd->argv.data[0], cmd->argv.data);
    print_error("Execvp failed");
    _exit(EXIT_FAILURE);
  } else {
    int status;
    if (waitpid(pid, &status, 0) == -1) {
      print_error("Waitpid failed");
      return -1;
    }

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      fprintf(stderr, "Command terminated by signal %d\n", WTERMSIG(status));
      return 128 + WTERMSIG(status);
    }
  }

  return -1;
}

char* command_to_string(Command* cmd) {
  if (cmd == NULL) return NULL;

  size_t total_length = 0;
  for (int i = 0; cmd->argv.data[i] != NULL; i++) {
    total_length += strnlen(cmd->argv.data[i], MAX_COMMAND_LENGTH) + 1;
    if (total_length > MAX_COMMAND_LENGTH) {
      print_error("Command length exceeds maximum allowed");
      return NULL;
    }
  }

  for (size_t i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    total_length += 3 + strnlen(redir->target, MAX_COMMAND_LENGTH);
    if (total_length > MAX_COMMAND_LENGTH) {
      print_error("Command length exceeds maximum allowed");
      return NULL;
    }
  }

  char* result = (char*)rmalloc(total_length + 1);
  if (result == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  memset(result, 0, total_length + 1);

  for (int i = 0; cmd->argv.data[i] != NULL; i++) {
    if (rstrcat(result, cmd->argv.data[i], total_length + 1) == NULL) {
      rfree((void*)result);
      return NULL;
    }
    if (cmd->argv.data[i + 1] != NULL) {
      if (rstrcat(result, " ", total_length + 1) == NULL) {
        rfree((void*)result);
        return NULL;
      }
    }
  }

  for (size_t i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    const char* redir_str = NULL;
    switch (redir->type) {
      case REDIRECT_INPUT:
        redir_str = " < ";
        break;
      case REDIRECT_OUTPUT:
        redir_str = " > ";
        break;
      case REDIRECT_APPEND:
        redir_str = " >> ";
        break;
      case REDIRECT_INPUT_DUP:
        redir_str = " <&";
        break;
      case REDIRECT_OUTPUT_DUP:
        redir_str = " >&";
        break;
      case REDIRECT_APPEND_DUP:
        redir_str = " >>&";
        break;
      default:
        print_error("Unknown redirect type");
        rfree((void*)result);
        return NULL;
    }
    if (rstrcat(result, redir_str, total_length + 1) == NULL ||
      rstrcat(result, redir->target, total_length + 1) == NULL) {
      rfree((void*)result);
      return NULL;
    }
  }

  if (cmd->background) {
    if (rstrcat(result, " &", total_length + 1) == NULL) {
      rfree((void*)result);
      return NULL;
    }
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
    rfree((void*)cmd_str);

    if (cmd->next != NULL)
      total_length += (cmd->and_next || cmd->or_next) ? 4 : (cmd->semi_next) ? 2 : 3;
    if (total_length > MAX_COMMAND_LENGTH) {
      print_error("Command list length exceeds maximum allowed");
      return NULL;
    }
    cmd = cmd->next;
  }

  char* result = (char*)rmalloc(total_length + 1);
  if (result == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  memset(result, 0, total_length + 1);

  cmd = list->head;
  while (cmd != NULL) {
    char* cmd_str = command_to_string(cmd);
    if (cmd_str == NULL) {
      rfree((void*)result);
      return NULL;
    }
    if (rstrcat(result, cmd_str, total_length + 1) == NULL) {
      rfree((void*)cmd_str);
      rfree((void*)result);
      return NULL;
    }
    rfree((void*)cmd_str);

    if (cmd->next != NULL) {
      const char* separator = cmd->and_next ? " && " :
                  cmd->or_next ? " || " :
                  cmd->semi_next ? "; " : " | ";
      if (rstrcat(result, separator, total_length + 1) == NULL) {
        rfree((void*)result);
        return NULL;
      }
    }
    cmd = cmd->next;
  }

  return result;
}

int execute_command_list(CommandList* list) {
  if (list == NULL || list->head == NULL) {
    print_error("Invalid command list");
    return -1;
  }

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
    if (command_line == NULL) {
      return -1;
    }
    status = execute_background_job(list, command_line);
    rfree((void*)command_line);
    return status;
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
      if (status == 0 && cmd->or_next)
        break;
    }
    if (cmd != NULL) cmd = cmd->next;
  }
  
  return status;
}

int parse_and_execute(const char* input) {
  if (input == NULL) {
    print_error("Invalid input");
    return -1;
  }
  
  size_t input_len = strnlen(input, MAX_INPUT_LENGTH);
  if (input_len >= MAX_INPUT_LENGTH) {
    print_error("Input exceeds maximum allowed length");
    return -1;
  }

  char* sanitized_input = (char*)rmalloc(input_len + 1);
  if (sanitized_input == NULL) {
    print_error("Memory allocation failed");
    return -1;
  }

  size_t sanitized_len = 0;
  for (size_t i = 0; i < input_len; i++) {
    if (isprint(input[i]) || isspace(input[i])) {
      sanitized_input[sanitized_len++] = input[i];
    }
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
            if (command_line != NULL) {
              execute_background_job(command_list, command_line);
              rfree((void*)command_line);
            }
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
  
  rfree((void*)sanitized_input);
  return result;
}