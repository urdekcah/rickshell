#include <stdio.h>
#include "error.h"

void print_error(const string message) {
  fprintf(stderr, "Error: %s\n", message.str);
}