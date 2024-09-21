#include <stdio.h>
#include "error.h"

void print_error(const char* message) {
  fprintf(stderr, "Error: %s\n", message);
}