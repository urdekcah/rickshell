#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wchar.h>
#include "builtin.h"
#include "expr.h"
#include "rstring.h"
#include "array.h"
#include "unicode.h"

static int process_escape(const string str, int *i) {
  int result = 0;
  char utf8_buffer[5] = {0};
  size_t utf8_len;

  switch (str.str[*i + 1]) {
    case '\\': result = putchar('\\'); break;
    case 'a': result = putchar('\a'); break;
    case 'b': result = putchar('\b'); break;
    case 'c': return -1;
    case 'e': case 'E': result = putchar('\033'); break;
    case 'f': result = putchar('\f'); break;
    case 'n': result = putchar('\n'); break;
    case 'r': result = putchar('\r'); break;
    case 't': result = putchar('\t'); break;
    case 'v': result = putchar('\v'); break;
    case '0':
      {
        int octal = 0;
        int j;
        for (j = 1; j <= 3 && str.str[*i + j] >= '0' && str.str[*i + j] <= '7'; j++)
          octal = octal * 8 + (str.str[*i + j] - '0');
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
    case 'u':
    case 'U':
      {
        int max_digits = (str.str[*i + 1] == 'u') ? 4 : 8;
        uint32_t unicode = 0;
        int j;
        for (j = 2; j < max_digits + 2 && (size_t)*i + (size_t)j < str.len; j++) {
          char c = str.str[*i + j];
          if (c >= '0' && c <= '9') {
            unicode = (unicode << 4) | ((uint32_t)c - '0');
          } else if (c >= 'a' && c <= 'f') {
            unicode = (unicode << 4) | ((uint32_t)c - 'a' + 10);
          } else if (c >= 'A' && c <= 'F') {
            unicode = (unicode << 4) | ((uint32_t)c - 'A' + 10);
          } else {
            break;
          }
        }
        UnicodeError err = unicode_to_utf8(unicode, utf8_buffer, sizeof(utf8_buffer), &utf8_len);
        if (err == UNICODE_SUCCESS) {
          for (size_t k = 0; k < utf8_len; k++)
            fprintf(stderr, "%02X ", (unsigned char)utf8_buffer[k]);
          fprintf(stderr, "\n");
          fwrite(utf8_buffer, 1, utf8_len, stdout);
        }
        *i += j - 1;
      }
      break;
    default:
      result = putchar('\\');
      if (result != EOF) result = putchar(str.str[*i + 1]);
  }
  return (result == EOF) ? -1 : 0;
}

int builtin_echo(Command *cmd) {
  bool interpret_escapes = false;
  bool print_newline = true;
  size_t start_index = 1;

  while (start_index < cmd->argv.size) {
    string elem = *(string*)array_get(cmd->argv, start_index);
    if (elem.str[0] != '-' || elem.len == 1) break;

    for (size_t j = 1; j < elem.len; j++) {
      switch (elem.str[j]) {
        case 'n':
          print_newline = false;
          break;
        case 'e':
          interpret_escapes = true;
          break;
        case 'E':
          interpret_escapes = false;
          break;
        default:
          goto end_option_processing;
      }
    }
    start_index++;
  }
end_option_processing:
  bool first_arg = true;
  for (size_t i = start_index; i < cmd->argv.size; i++) {
    if (!first_arg) {
      if (putchar(' ') == EOF) return 1;
    }
    first_arg = false;

    string elem = *(string*)array_checked_get(cmd->argv, i);
    if (interpret_escapes) {
      for (int j = 0; (size_t)j < elem.len; j++) {
        if (elem.str[j] == '\\' && (size_t)j + 1 < elem.len) {
          int result = process_escape(elem, &j);
          if (result == -1) {
            return 0;
          } else if (result != 0) {
            return 1;
          }
          j++;
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