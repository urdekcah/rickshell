#include <stdio.h>
#include "color.h"
#include "rstring.h"
#include "io.h"
#include "error.h"
#include "result.h"

void report_error(const Result r) {
  if (r.is_err) {
    ffprintln(stderr, ANSI_COLOR_BRIGHT_BOLD_RED "error:" ANSI_COLOR_RESET " %S", r.err.msg);
  }
}

void print_error(const string message) {
  ffprintln(stderr, "Error: %S", message);
}