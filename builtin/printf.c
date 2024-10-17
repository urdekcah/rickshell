#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include "builtin.h"
#include "expr.h"
#include "rstring.h"
#include "array.h"
#include "variable.h"
#include "unicode.h"
#include "strconv.h"
#include "io.h"

extern VariableTable* variable_table;

static string process_escape_sequences(const string input) {
  StringBuilder sb = string_builder__new();
  for (size_t i = 0; i < input.len; i++) {
    if (input.str[i] == '\\' && i + 1 < input.len) {
      switch (input.str[++i]) {
        case 'a': string_builder__append_char(&sb, '\a'); break;
        case 'b': string_builder__append_char(&sb, '\b'); break;
        case 'e': case 'E': string_builder__append_char(&sb, '\033'); break;
        case 'f': string_builder__append_char(&sb, '\f'); break;
        case 'n': string_builder__append_char(&sb, '\n'); break;
        case 'r': string_builder__append_char(&sb, '\r'); break;
        case 't': string_builder__append_char(&sb, '\t'); break;
        case 'v': string_builder__append_char(&sb, '\v'); break;
        case '\\': string_builder__append_char(&sb, '\\'); break;
        case '\'': string_builder__append_char(&sb, '\''); break;
        case '"': string_builder__append_char(&sb, '"'); break;
        case '?': string_builder__append_char(&sb, '\?'); break;
        case '0': case '1': case '2': case '3':
        case '4': case '5': case '6': case '7': {
          int octal = input.str[i] - '0';
          if (i + 1 < input.len && input.str[i + 1] >= '0' && input.str[i + 1] <= '7') {
            octal = octal * 8 + (input.str[++i] - '0');
            if (i + 1 < input.len && input.str[i + 1] >= '0' && input.str[i + 1] <= '7')
              octal = octal * 8 + (input.str[++i] - '0');
          }
          string_builder__append_char(&sb, (char)octal);
          break;
        }
        case 'x': case 'X': {
          if (i + 2 < input.len && isxdigit(input.str[i + 1]) && isxdigit(input.str[i + 2])) {
            int hex = (isdigit(input.str[i + 1]) ? input.str[i + 1] - '0' : 
                      (tolower(input.str[i + 1]) - 'a' + 10)) * 16 +
                      (isdigit(input.str[i + 2]) ? input.str[i + 2] - '0' : 
                      (tolower(input.str[i + 2]) - 'a' + 10));
            string_builder__append_char(&sb, (char)hex);
            i += 2;
          } else {
            string_builder__append_char(&sb, '\\');
            string_builder__append_char(&sb, input.str[i]);
          }
          break;
        }
        case 'u': case 'U': {
          int max_digits = (input.str[i] == 'u') ? 4 : 8;
          uint32_t unicode = 0;
          int j;
          for (j = 1; j <= max_digits && i + (size_t)j < input.len; j++) {
            char c = input.str[i + (size_t)j];
            if (isxdigit(c)) {
              unicode = (unicode << 4) | (uint32_t)(isdigit(c) ? c - '0' : (tolower(c) - 'a' + 10));
            } else {
              break;
            }
          }
          i += (size_t)j - 1;
          char utf8_buffer[5] = {0};
          size_t utf8_len;
          if (unicode_to_utf8(unicode, utf8_buffer, sizeof(utf8_buffer), &utf8_len) == UNICODE_SUCCESS) {
            string_builder__append_cstr(&sb, utf8_buffer);
          }
          break;
        }
        default:
          string_builder__append_char(&sb, '\\');
          string_builder__append_char(&sb, input.str[i]);
      }
    } else {
      string_builder__append_char(&sb, input.str[i]);
    }
  }
  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}

static string format_quoted_string(string s) {
  StringBuilder sb = string_builder__new();
  bool need_quotes = false;

  for (size_t i = 0; i < s.len; i++) {
    char c = s.str[i];
    switch (c) {
      case ' ':
      case '\t':
      case '\n':
      case '\\':
      case '\'':
      case '"':
      case '`':
      case '!':
      case '#':
      case '$':
      case '&':
      case '(':
      case ')':
      case '*':
      case ';':
      case '<':
      case '>':
      case '?':
      case '[':
      case ']':
      case '^':
      case '{':
      case '|':
      case '}':
      case '~':
        need_quotes = true;
        string_builder__append_char(&sb, '\\');
        string_builder__append_char(&sb, c);
        break;
      default:
        if (c < 32 || c > 126) {
          need_quotes = true;
          char octal[5];
          snprintf(octal, sizeof(octal), "\\%03o", (unsigned char)c);
          string_builder__append_cstr(&sb, octal);
        } else {
          string_builder__append_char(&sb, c);
        }
        break;
    }
  }

  string result;
  if (need_quotes) {
    StringBuilder final_sb = string_builder__new();
    string_builder__append_char(&final_sb, '\"');
    string temp = string_builder__to_string(&sb);
    string_builder__append(&final_sb, temp);
    string_builder__append_char(&final_sb, '\"');
    string__free(temp);
    result = string_builder__to_string(&final_sb);
    string_builder__free(&final_sb);
  } else {
    result = string_builder__to_string(&sb);
  }

  string_builder__free(&sb);
  return result;
}

static string format_time(const string format, time_t t) {
  struct tm* tm_info = localtime(&t);
  if (tm_info == NULL) {
    return string__new("Error: Unable to get local time");
  }

  StringBuilder sb = string_builder__new();
  for (size_t i = 0; i < format.len; i++) {
    if (format.str[i] == '%' && i + 1 < format.len) {
      char temp[512] = {0};
      size_t len = 0;

      switch (format.str[++i]) {
        case 'a': len = strftime(temp, sizeof(temp), "%a", tm_info); break;
        case 'A': len = strftime(temp, sizeof(temp), "%A", tm_info); break;
        case 'b': len = strftime(temp, sizeof(temp), "%b", tm_info); break;
        case 'B': len = strftime(temp, sizeof(temp), "%B", tm_info); break;
        case 'c': len = strftime(temp, sizeof(temp), "%c", tm_info); break;
        case 'C': len = strftime(temp, sizeof(temp), "%C", tm_info); break;
        case 'd': len = strftime(temp, sizeof(temp), "%d", tm_info); break;
        case 'D': len = strftime(temp, sizeof(temp), "%D", tm_info); break;
        case 'e': len = strftime(temp, sizeof(temp), "%e", tm_info); break;
        case 'F': len = strftime(temp, sizeof(temp), "%F", tm_info); break;
        case 'g': len = strftime(temp, sizeof(temp), "%g", tm_info); break;
        case 'G': len = strftime(temp, sizeof(temp), "%G", tm_info); break;
        case 'h': len = strftime(temp, sizeof(temp), "%h", tm_info); break;
        case 'H': len = strftime(temp, sizeof(temp), "%H", tm_info); break;
        case 'I': len = strftime(temp, sizeof(temp), "%I", tm_info); break;
        case 'j': len = strftime(temp, sizeof(temp), "%j", tm_info); break;
        case 'k': len = strftime(temp, sizeof(temp), "%k", tm_info); break;
        case 'l': len = strftime(temp, sizeof(temp), "%l", tm_info); break;
        case 'm': len = strftime(temp, sizeof(temp), "%m", tm_info); break;
        case 'M': len = strftime(temp, sizeof(temp), "%M", tm_info); break;
        case 'n': len = strftime(temp, sizeof(temp), "%n", tm_info); break;
        case 'p': len = strftime(temp, sizeof(temp), "%p", tm_info); break;
        case 'P': len = strftime(temp, sizeof(temp), "%P", tm_info); break;
        case 'r': len = strftime(temp, sizeof(temp), "%r", tm_info); break;
        case 'R': len = strftime(temp, sizeof(temp), "%R", tm_info); break;
        case 's': len = (size_t)snprintf(temp, sizeof(temp), "%ld", (long)t); break;
        case 'S': len = strftime(temp, sizeof(temp), "%S", tm_info); break;
        case 't': len = strftime(temp, sizeof(temp), "%t", tm_info); break;
        case 'T': len = strftime(temp, sizeof(temp), "%T", tm_info); break;
        case 'u': len = strftime(temp, sizeof(temp), "%u", tm_info); break;
        case 'U': len = strftime(temp, sizeof(temp), "%U", tm_info); break;
        case 'V': len = strftime(temp, sizeof(temp), "%V", tm_info); break;
        case 'w': len = strftime(temp, sizeof(temp), "%w", tm_info); break;
        case 'W': len = strftime(temp, sizeof(temp), "%W", tm_info); break;
        case 'x': len = strftime(temp, sizeof(temp), "%x", tm_info); break;
        case 'X': len = strftime(temp, sizeof(temp), "%X", tm_info); break;
        case 'y': len = strftime(temp, sizeof(temp), "%y", tm_info); break;
        case 'Y': len = strftime(temp, sizeof(temp), "%Y", tm_info); break;
        case 'z': len = strftime(temp, sizeof(temp), "%z", tm_info); break;
        case 'Z': len = strftime(temp, sizeof(temp), "%Z", tm_info); break;
        case '%': len = strftime(temp, sizeof(temp), "%%", tm_info); break;
        default:
          string_builder__append_char(&sb, '%');
          string_builder__append_char(&sb, format.str[i]);
          continue;
      }

      if (len > 0) {
        string_builder__append_cstr(&sb, temp);
      }
    } else {
      string_builder__append_char(&sb, format.str[i]);
    }
  }

  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}

int builtin_printf(Command *cmd) {
  if (cmd->argv.size < 2) {
    ffprintln(stderr, "printf: usage: printf [-v var] format [arguments]");
    return 1;
  }

  string var_name = _SLIT0;
  size_t arg_start = 1;

  if (string__equals(*(string*)array_get(cmd->argv, 1), _SLIT("-v"))) {
    if (cmd->argv.size < 4) {
      ffprintln(stderr, "printf: -v requires a variable name");
      return 1;
    }
    var_name = *(string*)array_get(cmd->argv, 2);
    arg_start = 3;
  }

  string format = *(string*)array_get(cmd->argv, arg_start);
  format = process_escape_sequences(format);

  StringBuilder output = string_builder__new();
  size_t arg_index = arg_start + 1;

  for (size_t i = 0; i < format.len; i++) {
    if (format.str[i] == '%' && i + 1 < format.len) {
      i++;
      switch (format.str[i]) {
        case '%':
          string_builder__append_char(&output, '%');
          break;
        case 's': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            string_builder__append(&output, arg);
          }
          break;
        }
        case 'd': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            int value;
            StrconvResult result;
            result = ratoi(arg, &value);
            if (!result.is_err) {
              result = ritoa(value);
              if (!result.is_err) {
                string num_str = *(string*)result.value;
                string_builder__append(&output, num_str);
                string__free(num_str);
              } else {
                ffprintln(stderr, "printf: invalid number: %S", arg);
              }
            } else {
              ffprintln(stderr, "printf: invalid number: %S", arg);
            }
          }
          break;
        }
        case 'f': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            double value;
            StrconvResult result;
            result = ratod(arg, &value);
            if (!result.is_err) {
              result = rdtos(value);
              if (!result.is_err) {
                string num_str = *(string*)result.value;
                string_builder__append(&output, num_str);
                string__free(num_str);
              } else {
                ffprintln(stderr, "printf: invalid float: %S", arg);
              }
            } else {
              ffprintln(stderr, "printf: invalid float: %S", arg);
            }
          }
          break;
        }
        case 'x': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            long value;
            StrconvResult result = ratol(arg, &value);
            if (!result.is_err) {
              StringBuilder hex_sb = string_builder__new();
              char hex_buffer[16];
              int hex_index = 0;
              do {
                int digit = value & 0xF;
                hex_buffer[hex_index++] = (char)(digit < 10 ? '0' + digit : 'a' + (digit - 10));
                value >>= 4;
              } while (value);
              for (int j = hex_index - 1; j >= 0; j--) {
                char hex_digit[2] = {hex_buffer[j], '\0'};
                string_builder__append_cstr(&hex_sb, hex_digit);
              }
              string hex_str = string_builder__to_string(&hex_sb);
              string_builder__append(&output, hex_str);
              string__free(hex_str);
              string_builder__free(&hex_sb);
            } else {
              ffprintln(stderr, "printf: invalid number: %S", arg);
            }
          }
          break;
        }
        case 'b': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            string processed = process_escape_sequences(arg);
            string_builder__append(&output, processed);
            string__free(processed);
          }
          break;
        }
        case 'q': {
          if (arg_index < cmd->argv.size) {
            string arg = *(string*)array_get(cmd->argv, arg_index++);
            string quoted = format_quoted_string(arg);
            string_builder__append(&output, quoted);
            string__free(quoted);
          }
          break;
        }
        case 'T': {
          if (i + 1 < format.len && format.str[i + 1] == '(') {
            size_t end = i + 2;
            while (end < format.len && format.str[end] != ')') end++;
            if (end < format.len) {
              string time_format = string__substring(format, (ssize_t)i + 2, (ssize_t)end);
              time_t t = (arg_index < cmd->argv.size) ? atoi(((string*)array_get(cmd->argv, arg_index++))->str) : -1;
              if (t == -1) t = time(NULL);
              string time_str = format_time(time_format, t);
              string_builder__append(&output, time_str);
              string__free(time_str);
              string__free(time_format);
              i = end;
            }
          }
          break;
        }
        default:
          ffprintln(stderr, "printf: invalid format character: %c", format.str[i]);
          string_builder__free(&output);
          string__free(format);
          return 1;
      }
    } else {
      string_builder__append_char(&output, format.str[i]);
    }
  }

  string result = string_builder__to_string(&output);
  string_builder__free(&output);
  string__free(format);

  if (var_name.len > 0) {
    set_variable(variable_table, var_name, result, VAR_STRING, false);
  } else {
    fprint("%S", result);
  }

  string__free(result);
  return 0;
}