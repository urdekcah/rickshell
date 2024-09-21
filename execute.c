#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include "expr.h"
#include "pipeline.h"
#include "redirect.h"
#include "execute.h"
#include "memory.h"
#include "error.h"

extern int yyparse(void);
extern void yylex_destroy(void);
extern void yy_scan_string(const char *str);

#define MAX_INPUT_LENGTH 4096

extern CommandList* command_list;

int execute_command_list(CommandList* list) {
  int status = 0;
  Command* cmd = list->head;
  
  while (cmd != NULL) {
    status = execute_pipeline(cmd);
    
    while (cmd != NULL && !cmd->and_next && !cmd->or_next) {
      cmd = cmd->next;
    }

    if (cmd != NULL) {
      if ((cmd->and_next && status != 0) || (cmd->or_next && status == 0)) {
        cmd = cmd->next;
      }
    }
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

  yy_scan_string(sanitized_input);
  command_list = NULL;
  int result = yyparse();
  yylex_destroy();
    
  free(sanitized_input);
    
  if (result == 0) {
    if (command_list != NULL) {      
      int status = execute_command_list(command_list);
      (void)status;
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