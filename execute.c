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

extern CommandList* command_list;
extern VariableTable* variable_table;

int rexecvp(const string __file, StringArray __argv) {
  char* file_cstr = string__to_cstr(__file);
  if (!file_cstr) {
    errno = ENOMEM;
    return -1;
  }

  size_t argc = __argv.size;
  register size_t i;

  char** argv_cstr = malloc((argc + 1) * sizeof(char*));
  if (!argv_cstr) {
    free(file_cstr);
    errno = ENOMEM;
    return -1;
  }

  for (i = 0; i < argc; i++) {
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

  for (i = 0; i < argc; i++)
    free(argv_cstr[i]);
  free(argv_cstr);
  free(file_cstr);

  errno = saved_errno;
  return result;
}

IntResult execute_command(Command* cmd, int* result) {
  register size_t i;
  if (cmd == NULL || cmd->argv.size == 0 || string__is_null_or_empty(*(string*)array_get(cmd->argv, 0))) return Err(
    _SLIT("Invalid command"),
    ERRCODE_INVALID_ARGUMENT
  );

  for (i = 0; i < cmd->argv.size; i++) {
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

    if (open_bracket_index != -1 && close_bracket_index != -1 && open_bracket_index < close_bracket_index && equals_sign_index) {
      string key = string__substring(felem, open_bracket_index + 1, close_bracket_index);
      string _name = string__substring(felem, 0, open_bracket_index);
      string _value = (cmd->argv.size > 1) ? string__from(*(string*)array_get(cmd->argv, 1)) : _SLIT("");

      Variable* var = get_variable(variable_table, _name);
      if (var != NULL && var->value.type == VAR_ASSOCIATIVE_ARRAY) {
        set_associative_array_variable(variable_table, _name, key, _value);
      } else if (var != NULL && var->value.type == VAR_ARRAY) {
        long long index;
        StrconvResult _result = ratoll(key, &index);
        if (_result.is_err) {
          string__free(_name);
          string__free(_value);
          string__free(key);
          return Err(
            _SLIT("Invalid key for array"),
            ERRCODE_ARRAY_INVALID_INDEX
          );
        }
        array_set_element(variable_table, _name, (size_t)index, _value);
      } else {
        return Err(
          _SLIT("Variable is not an array or associative array"),
          ERRCODE_VAR_TYPE_DISMATCH
        );
      }
      string__free(_name);
      string__free(_value);
      string__free(key);
    } else if (string__startswith(value, _SLIT("(")) && string__endswith(value, _SLIT(")"))) {
      parse_and_set_array(variable_table, name, value);
    } else if (string__startswith(value, _SLIT("{")) && string__endswith(value, _SLIT("}"))) {
      parse_and_set_associative_array(variable_table, name, value);
    } else {
      Variable* var = set_variable(variable_table, name, value, parse_variable_type(value), false);
      if (var == NULL) {
        string__free(name);
        string__free(value);
        return Err(
          _SLIT("Failed to set variable"),
          ERRCODE_VAR_SET_FAILED
        );
      }
    }
    string__free(name);
    string__free(value);
    return Ok(NULL);
  } else if (should_not_expand) {
    StringArray _argv = array_clone_from(cmd->argv);
    array_free(&cmd->argv);
    cmd->argv = create_array_with_capacity(sizeof(string), _argv.capacity);
    for (i = 0; i < _argv.size; i++) {
      string elem = *(string*)array_get(_argv, i);
      if (string__length(elem) > 0 && string__indexof(elem, _SLIT("=")) != -1) {
        if (i + 1 < _argv.size) {
          string next = *(string*)array_get(_argv, i + 1);
          string new_elem = string__concat(elem, next);
          string__free(elem);
          string__free(next);
          array_push(&cmd->argv, &new_elem);
          i++;
        } else {
          array_push(&cmd->argv, &elem);
        }
      } else {
        array_push(&cmd->argv, &elem);
      }
    }
    array_free(&_argv);
  }

  *result = execute_builtin(cmd);
  if (*result != -1)
    return Ok(NULL);

  pid_t pid = fork();
  if (pid == -1) {
    return Err(
      _SLIT("Fork failed"),
      ERRCODE_EXEC_FORK_FAILED
    );
  } else if (pid == 0) {
    Result r = handle_redirection(cmd);
    if (r.is_err) {
      report_error(r);
      _exit(EXIT_FAILURE);
    }

    rexecvp(felem, cmd->argv);
    print_error(_SLIT("Execvp failed"));
    _exit(EXIT_FAILURE);
  } else {
    int status;
    if (waitpid(pid, &status, 0) == -1) {
      return Err(
        _SLIT("Waitpid failed"),
        ERRCODE_EXEC_WAIT_FAILED
      );
    }

    if (WIFEXITED(status)) {
      *result = WEXITSTATUS(status);
      return Ok(NULL);
    } else if (WIFSIGNALED(status)) {
      ffprintln(stderr, "Command terminated by signal %d", WTERMSIG(status));
      *result = 128 + WTERMSIG(status);
      return Ok(NULL);
    }
  }

  *result = -1;
  return Ok(NULL);
}

string command_to_string(Command* cmd) {
  if (cmd == NULL) return _SLIT0;
  register size_t i;

  StringBuilder sb = string_builder__new();
  for (i = 0; i < cmd->argv.size; i++) {
    string elem = *(string*)array_get(cmd->argv, i);
    string_builder__append(&sb, elem);
    if (i < cmd->argv.size - 1) {
      string_builder__append(&sb, _SLIT(" "));
    }
  }

  for (i = 0; i < cmd->redirects.size; i++) {
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

IntResult execute_command_list(CommandList* list, int* result) {
  if (list == NULL || list->head == NULL) return Err(
    _SLIT("Invalid command list"),
    ERRCODE_INVALID_ARGUMENT
  );

  Command* cmd = list->head;

  bool background = false;
  Command* last_cmd = list->tail;
  if (last_cmd && last_cmd->background) {
    background = true;
    last_cmd->background = false;
  }

  if (background) {
    string command_line = command_list_to_string(list);
    if (string__is_null_or_empty(command_line)) return Err(
      _SLIT("Failed to convert command list to string"),
      ERRCODE_ERR
    );
    Result r = execute_background_job(list, command_line, result);
    string__free(command_line);
    NTRY(r);
    return Ok(NULL);
  }

  while (cmd != NULL) {
    if (cmd->pipline_next) {
      Result r = execute_pipeline(cmd, result);
      NTRY(r);
      while (cmd != NULL && cmd->pipline_next)
        cmd = cmd->next;
    } else {
      Result r = execute_command(cmd, result);
      NTRY(r);
      if (*result != 0 && cmd->and_next)
        break;
      if (*result == 0 && cmd->or_next)
        break;
    }
    if (cmd != NULL) cmd = cmd->next;
  }
  
  return Ok(NULL);
}

IntResult parse_and_execute(const string input, int* result) {
  if (string__is_null_or_empty(input)) {
    return Err(
      _SLIT("Invalid input"),
      ERRCODE_INVALID_INPUT
    );
  }

  char* saveptr;
  char* cmd = strtok_r(input.str, ";", &saveptr);
  *result = 0;

  while (cmd != NULL) {
    while (isspace(*cmd)) cmd++;
    if (*cmd != '\0') {
      yy_scan_string(cmd);
      command_list = NULL;
      *result = yyparse();
      yylex_destroy();
      
      if (*result == 0) {
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
              Result r = execute_background_job(command_list, command_line, result);
              string__free(command_line);
              NTRY(r);
            }
          } else {
            Result r = execute_command_list(command_list, result);
            NTRY(r);
          }
          free_command_list(command_list);
          command_list = NULL;
        } else {
          return Err(
            _SLIT("No commands to execute"),
            ERRCODE_NO_COMMAND_TO_EXECUTE
          );
        }
      } else {
        return Err(
          _SLIT("Failed to parse the command"),
          ERRCODE_COMMAND_PARSE_FAILED
        );
      }
    }
    cmd = strtok_r(NULL, ";", &saveptr);
  }
  
  return Ok(NULL);
}