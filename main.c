#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include "execute.h"
#include "parser.tab.h"

#define MAX_INPUT_SIZE 1024

extern int yylex_destroy(void);

void handle_sigint(int sig) {
  printf("\nUse 'exit' to quit the shell.\n");
  printf("> ");
  fflush(stdout);
}

int main(/* int argc, char* argv[] */) {
  char input[MAX_INPUT_SIZE];

  signal(SIGINT, handle_sigint);

  printf("Rickshell Parser\n");
  printf("Enter commands (type 'exit' to quit):\n");

  while (1) {
    printf("> ");
    fflush(stdout);

    if (fgets(input, sizeof(input), stdin) == NULL) {
      if (feof(stdin)) {
        printf("\nEnd of input. Exiting...\n");
        break;
      } else {
        perror("Error reading input");
        continue;
      }
    }

    input[strcspn(input, "\n")] = 0;

    if (strlen(input) == 0) {
      continue;
    }

    if (strcmp(input, "exit") == 0) {
      printf("Exiting...\n");
      break;
    }

    int result = parse_and_execute(input);
    printf((result == 0) ? "Command executed successfully\n" : "Command execution failed\n");

    yylex_destroy();
  }

  return 0;
}