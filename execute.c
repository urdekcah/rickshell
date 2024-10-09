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
#include "io.h"
#include "job.h"
#include "memory.h"
#include "error.h"
#include "variable.h"
#include "strconv.h"
#include "rstring.h"
#include "array.h"

extern int yyparse(void);
extern int yylex_destroy(void);
extern void yy_scan_string(const char *str);

#define MAX_INPUT_LENGTH 4096
#define MAX_COMMAND_LENGTH 1024

extern CommandList* command_list;
extern VariableTable* variable_table;

int rexecvp(const string __file, StringArray __argv) {
  char* file_cstr = string__to_cstr(__file);
  if (!file_cstr) {
    errno = ENOMEM;
    return -1;
  }

  size_t argc = __argv.size;

  char** argv_cstr = malloc((argc + 1) * sizeof(char*));
  if (!argv_cstr) {
    free(file_cstr);
    errno = ENOMEM;
    return -1;
  }

  for (size_t i = 0; i < argc; i++) {
    string* arg = array_get(__argv, i);
    argv_cstr[i] = string__to_cstr(*arg);
    if (!argv_cstr[i]) {
      for (size_t j = 0; j < i; j++)
        free(argv_cstr[j]);
      free(argv_cstr);
      free(file_cstr);
      errno = ENOMEM;
      return -1;
    }
  }
  argv_cstr[argc] = NULL;
  int result = execvp(file_cstr, argv_cstr);

  int saved_errno = errno;

  for (size_t i = 0; i < argc; i++)
    free(argv_cstr[i]);
  free(argv_cstr);
  free(file_cstr);

  errno = saved_errno;
  return result;
}

int execute_command(Command* cmd) {
  if (cmd == NULL || cmd->argv.size == 0 || string__is_null_or_empty(*(string*)array_get(cmd->argv, 0))) {
    print_error(_SLIT("Invalid command"));
    return -1;
  }

  for (size_t i = 0; i < cmd->argv.size; i++) {
    string elem = *(string*)array_get(cmd->argv, i);
    string expanded = expand_variables(variable_table, elem);
    string__free(elem);
    array_index_set(&cmd->argv, i, &expanded);
  }

  string felem = *(string*)array_get(cmd->argv, 0);
  bool should_not_expand = do_not_expand_this_builtin(felem);
  ssize_t equals_sign_index = string__indexof(felem, _SLIT("="));
  ssize_t open_bracket_index = string__indexof(felem, _SLIT("["));
  ssize_t close_bracket_index = string__lastindexof(felem, _SLIT("]"));
  if (equals_sign_index != -1 && !should_not_expand) {
    string name = string__substring(felem, 0, equals_sign_index);
    string value = string__substring(felem, equals_sign_index + 1);

    if (string__is_null_or_empty(value) && cmd->argv.size > 1) {
      string next = *(string*)array_get(cmd->argv, 1);
      string temp = string__from(next);
      string__free(value);
      value = temp;
    }

    if (string__startswith(value, _SLIT("(")) && string__endswith(value, _SLIT(")"))) {
      parse_and_set_array(variable_table, name, value);
    } else if (string__startswith(value, _SLIT("{")) && string__endswith(value, _SLIT("}"))) {
      parse_and_set_associative_array(variable_table, name, value);
    } else if (open_bracket_index != -1 && close_bracket_index != -1 && open_bracket_index < close_bracket_index) {
      string key = string__substring(felem, open_bracket_index + 1, close_bracket_index);
      string _value = string__substring(felem, close_bracket_index + 1);
      
      Variable* var = get_variable(variable_table, name);
      if (var != NULL && var->value.type == VAR_ASSOCIATIVE_ARRAY) {
        set_associative_array_variable(variable_table, name, key, _value);
      } else if (var != NULL && var->value.type == VAR_ARRAY) {
        long long index;
        StrconvResult result = ratoll(key, &index);
        if (result.is_err) {
          print_error(_SLIT("Invalid key for associative array"));
          return -1;
        }
        array_set_element(variable_table, name, (size_t)index, _value);
      } else {
        print_error(_SLIT("Variable is not an array or associative array"));
        return -1;
      }
    } else {
      Variable* var = set_variable(variable_table, name, value, parse_variable_type(value), false);
      if (var == NULL) {
        print_error(_SLIT("Failed to set variable"));
        return -1;
      }
    }
    string__free(name);
    return 0;
  } else if (should_not_expand) {
    StringArray new_data = create_array(cmd->argv.size);
    for (size_t i = 0; i < cmd->argv.size; i++) {
      string elem = *(string*)array_get(cmd->argv, i);
      if (string__length(elem) > 0 && string__indexof(elem, _SLIT("=")) != -1) {
        if (i + 1 < cmd->argv.size) {
          string next = *(string*)array_get(cmd->argv, i + 1);
          string new_elem = string__concat(elem, next);
          string__free(elem);
          string__free(next);
          array_push(&new_data, &new_elem);
          i++;
        } else {
          array_push(&new_data, &elem);
        }
      } else {
        array_push(&new_data, &elem);
      }
    }
    array_free(&cmd->argv);
    cmd->argv = new_data;
  }

  int builtin_result = execute_builtin(cmd);
  if (builtin_result != -1)
    return builtin_result;

  pid_t pid = fork();
  if (pid == -1) {
    print_error(_SLIT("Fork failed"));
    return -1;
  } else if (pid == 0) {
    if (handle_redirection(cmd) == -1) {
      _exit(EXIT_FAILURE);
    }

    rexecvp(felem, cmd->argv);
    print_error(_SLIT("Execvp failed"));
    _exit(EXIT_FAILURE);
  } else {
    int status;
    if (waitpid(pid, &status, 0) == -1) {
      print_error(_SLIT("Waitpid failed"));
      return -1;
    }

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
      ffprintln(stderr, "Command terminated by signal %d", WTERMSIG(status));
      return 128 + WTERMSIG(status);
    }
  }

  return -1;
}

string command_to_string(Command* cmd) {
  if (cmd == NULL) return _SLIT0;

  StringBuilder sb = string_builder__new();
  for (size_t i = 0; i < cmd->argv.size; i++) {
    string elem = *(string*)array_get(cmd->argv, i);
    string_builder__append(&sb, elem);
    if (i < cmd->argv.size - 1) {
      string_builder__append(&sb, _SLIT(" "));
    }
  }

  for (size_t i = 0; i < cmd->redirects.size; i++) {
    Redirect* redir = &cmd->redirects.data[i];
    string redir_str = _SLIT0;
    switch (redir->type) {
      case REDIRECT_INPUT:
        redir_str = _SLIT(" < ");
        break;
      case REDIRECT_OUTPUT:
        redir_str = _SLIT(" > ");
        break;
      case REDIRECT_APPEND:
        redir_str = _SLIT(" >> ");
        break;
      case REDIRECT_INPUT_DUP:
        redir_str = _SLIT(" <&");
        break;
      case REDIRECT_OUTPUT_DUP:
        redir_str = _SLIT(" >&");
        break;
      case REDIRECT_APPEND_DUP:
        redir_str = _SLIT(" >>&");
        break;
      default:
        print_error(_SLIT("Unknown redirect type"));
        string_builder__free(&sb);
        return _SLIT0;
    }

    string_builder__append(&sb, redir_str);
    string target = string__from(redir->target);
    string_builder__append(&sb, target);
  }

  if (cmd->background) 
    string_builder__append(&sb, _SLIT(" &"));

  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}

string command_list_to_string(CommandList* list) {
  if (list == NULL || list->head == NULL) return _SLIT0;

  Command* cmd = list->head;
  StringBuilder sb = string_builder__new();
  while (cmd != NULL) {
    string cmd_str = command_to_string(cmd);
    if (string__is_null_or_empty(cmd_str)) {
      string_builder__free(&sb);
      return _SLIT0;
    }
    string_builder__append(&sb, cmd_str);
    string__free(cmd_str);

    if (cmd->next != NULL) {
      const string separator = cmd->and_next ? _SLIT(" && ") :
                               cmd->or_next ? _SLIT(" || ") :
                               cmd->semi_next ? _SLIT("; ") : _SLIT(" | ");
      string_builder__append(&sb, separator);
    }
    cmd = cmd->next;
  }

  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}

int execute_command_list(CommandList* list) {
  if (list == NULL || list->head == NULL) {
    print_error(_SLIT("Invalid command list"));
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
    string command_line = command_list_to_string(list);
    if (string__is_null_or_empty(command_line)) return -1;
    status = execute_background_job(list, command_line);
    string__free(command_line);
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

int parse_and_execute(const string input) {
  if (string__is_null_or_empty(input)) {
    print_error(_SLIT("Invalid input"));
    return -1;
  }
  
  size_t input_len = strnlen(input.str, MAX_INPUT_LENGTH);
  if (input_len >= MAX_INPUT_LENGTH) {
    print_error(_SLIT("Input exceeds maximum allowed length"));
    return -1;
  }

  char* sanitized_input = (char*)rmalloc(input_len + 1);
  if (sanitized_input == NULL) {
    print_error(_SLIT("Memory allocation failed"));
    return -1;
  }

  size_t sanitized_len = 0;
  for (size_t i = 0; i < input_len; i++) {
    if (isprint(input.str[i]) || isspace(input.str[i])) {
      sanitized_input[sanitized_len++] = input.str[i];
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
            string command_line = command_list_to_string(command_list);
            if (!string__is_null_or_empty(command_line)) {
              execute_background_job(command_list, command_line);
              string__free(command_line);
            }
          } else {
            int status = execute_command_list(command_list);
            (void)status;
          }
          free_command_list(command_list);
          command_list = NULL;
        } else {
          println(_SLIT("No commands to execute"));
        }
      } else {
        println(_SLIT("Failed to parse the command"));
        break;
      }
    }
    cmd = strtok_r(NULL, ";", &saveptr);
  }
  
  rfree((void*)sanitized_input);
  return result;
}