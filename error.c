#include <stdio.h>
#include "rstring.h"
#include "io.h"
#include "error.h"

void print_error(const string message) {
  ffprintln(stderr, "Error: %S", message);
}