#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include "builtin.h"
#include "expr.h"
#include "rstring.h"
#include "array.h"

static int process_escape(const string str, int *i) {
  int result = 0;
  switch (str.str[*i + 1]) {
    case '\\': result = putchar('\\'); break;
    case 'a': result = putchar('\a'); break;
    case 'b': result = putchar('\b'); break;
    case 'c': return -1;
    case 'e': result = putchar('\e'); break;
    case 'f': result = putchar('\f'); break;
    case 'n': result = putchar('\n'); break;
    case 'r': result = putchar('\r'); break;
    case 't': result = putchar('\t'); break;
    case 'v': result = putchar('\v'); break;
    case '0':
      {
        int octal = 0;
        int j;
        for (j = 1; j <= 3 && str.str[*i + j] >= '0' && str.str[*i + j] <= '7'; j++) {
          octal = octal * 8 + (str.str[*i + j] - '0');
        }
        result = putchar(octal);
        *i += j - 1;
      }
      break;
    case 'x':
      {
        int hex = 0;
        int j;
        for (j = 1; j <= 2 && ((str.str[*i + j] >= '0' && str.str[*i + j] <= '9') ||
                               (str.str[*i + j] >= 'a' && str.str[*i + j] <= 'f') ||
                               (str.str[*i + j] >= 'A' && str.str[*i + j] <= 'F')); j++) {
          hex = hex * 16 + (str.str[*i + j] <= '9' ? str.str[*i + j] - '0' :
                           (str.str[*i + j] | 32) - 'a' + 10);
        }
        result = putchar(hex);
        *i += j - 1;
      }
      break;
    default:
      result = putchar('\\');
      if (result != EOF) result = putchar(str.str[*i + 1]);
  }
  (*i)++;
  return (result == EOF) ? -1 : 0;
}

int builtin_echo(Command *cmd) {
  bool interpret_escapes = false;
  bool print_newline = true;
  size_t start_index = 1;

  while (start_index < cmd->argv.size && (*(string*)array_get(cmd->argv, start_index)).str[0] == '-') {
    string elem = *(string*)array_get(cmd->argv, start_index);
    if (string__compare(elem, _SLIT("-n")) == 0) {
      print_newline = false;
    } else if (string__compare(elem, _SLIT("-e")) == 0) {
      interpret_escapes = true;
    } else if (string__compare(elem, _SLIT("-E")) == 0) {
      interpret_escapes = false;
    } else if (elem.len > 1 && elem.str[1] == '\0') {
      break;
    } else {
      break;
    }
    start_index++;
  }

  bool first_arg = true;
  for (size_t i = start_index; i < cmd->argv.size; i++) {
    if (!first_arg) {
      if (putchar(' ') == EOF) return 1;
    }
    first_arg = false;

    string elem = *(string*)array_checked_get(cmd->argv, i);
    if (interpret_escapes) {
      for (int j = 0; !(string__is_null_or_empty(elem)); j++) {
        if (elem.str[j] == '\\' && elem.str[j + 1] != '\0') {
          int result = process_escape(elem, &j);
          if (result == -1) {
            return 0;
          } else if (result != 0) {
            return 1;
          }
        } else {
          if (putchar(elem.str[j]) == EOF) return 1;
        }
      }
    } else {
      if (fputs(elem.str, stdout) == EOF) return 1;
    }
  }

  if (print_newline) {
    if (putchar('\n') == EOF) return 1;
  }

  return 0;
}