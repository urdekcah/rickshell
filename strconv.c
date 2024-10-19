#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include "result.h"
#include "rstring.h"
#include "strconv.h"
#include "io.h"

StrconvResult ratoi(const string str, int* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  char *endptr;
  errno = 0;
  long val = strtol(str.str, &endptr, 10);
  if (errno == ERANGE) {
    return Err(
      _SLIT("Failed to convert string to int"),
      (val == LONG_MAX) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  if (endptr == str.str || *endptr != '\0') return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  if (val > INT_MAX || val < INT_MIN) {
    return Err(
      _SLIT("Failed to convert string to int"),
      (val > 0) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  *out = (int)val;
  return Ok(NULL);
}

StrconvResult ratol(const string str, long* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  char *endptr;
  errno = 0;
  long val = strtol(str.str, &endptr, 10);
  if (errno == ERANGE) {
    return Err(
      _SLIT("Failed to convert string to long"),
      (val == LONG_MAX) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  if (endptr == str.str || *endptr != '\0') return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  *out = val;
  return Ok(NULL);
}

StrconvResult ratoll(const string str, long long* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  char *endptr;
  errno = 0;
  long long val = strtoll(str.str, &endptr, 10);
  if (errno == ERANGE) {
    return Err(
      _SLIT("Failed to convert string to long long"),
      (val == LLONG_MAX) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  if (endptr == str.str || *endptr != '\0') return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  *out = val;
  return Ok(NULL);
}

StrconvResult ratof(const string str, float* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  char *endptr;
  errno = 0;
  float val = strtof(str.str, &endptr);
  if (errno == ERANGE) {
    return Err(
      _SLIT("Failed to convert string to float"),
      (val == HUGE_VALF) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  if (endptr == str.str || *endptr != '\0') return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  *out = val;
  return Ok(NULL);
}

StrconvResult ratod(const string str, double* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  char *endptr;
  errno = 0;
  double val = strtod(str.str, &endptr);
  if (errno == ERANGE) {
    return Err(
      _SLIT("Failed to convert string to double"),
      (val == HUGE_VAL) ? ERRCODE_STRCONV_OVERFLOW : ERRCODE_STRCONV_UNDERFLOW
    );
  }
  if (endptr == str.str || *endptr != '\0') return Err(
    _SLIT("Invalid input"),
    ERRCODE_INVALID_INPUT
  );
  *out = val;
  return Ok(NULL);
}

StrconvResult ritoa(int value) {
  StringBuilder sb = string_builder__new();
  string_builder__append_int(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (string__is_null_or_empty(s)) {
    string__free(s);
    return Err(
      _SLIT("Failed to convert int to string"),
      ERRCODE_STRCONV_CONVERSION_FAILED
    );
  }
  return Ok((void*)&s);
}

StrconvResult rltos(long value) {
  StringBuilder sb = string_builder__new();
  string_builder__append_long(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (string__is_null_or_empty(s)) {
    string__free(s);
    return Err(
      _SLIT("Failed to convert long to string"),
      ERRCODE_STRCONV_CONVERSION_FAILED
    );
  }
  return Ok((void*)&s);
}

StrconvResult rlltos(long long value) {
  StringBuilder sb = string_builder__new();
  string_builder__append_long_long(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (string__is_null_or_empty(s)) {
    string__free(s);
    return Err(
      _SLIT("Failed to convert long long to string"),
      ERRCODE_STRCONV_CONVERSION_FAILED
    );
  }
  return Ok((void*)&s);
}

StrconvResult rftos(float value) {
  StringBuilder sb = string_builder__new();
  string_builder__append_float(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (string__is_null_or_empty(s)) {
    string__free(s);
    return Err(
      _SLIT("Failed to convert float to string"),
      ERRCODE_STRCONV_CONVERSION_FAILED
    );
  }
  return Ok((void*)&s);
}

StrconvResult rdtos(double value) {
  StringBuilder sb = string_builder__new();
  string_builder__append_double(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (string__is_null_or_empty(s)) {
    string__free(s);
    return Err(
      _SLIT("Failed to convert double to string"),
      ERRCODE_STRCONV_CONVERSION_FAILED
    );
  }
  return Ok((void*)&s);
}

static void format_integer(char* buffer, size_t buffer_size, uintmax_t value, FormatSpecifier* spec) {
  char tmp[65];
  int len = 0;
  const char* digits = (spec->type == FmtUint || spec->uppercase) ? "0123456789ABCDEF" : "0123456789abcdef";

  do {
    tmp[len++] = digits[value % (uintmax_t)spec->base];
    value /= (uintmax_t)spec->base;
  } while (value && len < 65);

  if (spec->type == FmtInt && (intmax_t)value < 0) {
    tmp[len++] = '-';
  } else if (spec->show_sign) {
    tmp[len++] = '+';
  } else if (spec->space_prefix) {
    tmp[len++] = ' ';
  }

  if (spec->alternative_form) {
    if (spec->base == 16) {
      tmp[len++] = spec->uppercase ? 'X' : 'x';
      tmp[len++] = '0';
    } else if (spec->base == 8 && tmp[len-1] != '0') {
      tmp[len++] = '0';
    }
  }

  int min_width = (spec->precision > len) ? spec->precision : len;
  size_t padding = (spec->width > min_width) ? (size_t)(spec->width - min_width) : 0;
  size_t total_len = (size_t)min_width + padding;
  register ssize_t i;

  if (total_len >= buffer_size) {
    total_len = buffer_size - 1;
    min_width = (int)(total_len - padding);
  }

  size_t index = 0;
  if (!spec->left_justify) {
    char pad_char = (spec->zero_pad && spec->precision < 0) ? '0' : ' ';
    for (i = 0; i < (ssize_t)padding; i++) {
      buffer[index++] = pad_char;
    }
  }

  for (i = (ssize_t)len; i < min_width; i++) {
    buffer[index++] = '0';
  }

  for (i = (ssize_t)len - 1; i >= 0; i--) {
    buffer[index++] = tmp[i];
  }

  if (spec->left_justify) {
    for (i = 0; i < (ssize_t)padding; i++) {
      buffer[index++] = ' ';
    }
  }

  buffer[index] = '\0';
}

static void format_float(char* buffer, size_t buffer_size, long double value, FormatSpecifier* spec) {
  char format[32];
  char tmp[128];
  
  char type_char = spec->type == FmtDouble ? 'f' : 
           (spec->type == FmtExponent ? (spec->uppercase ? 'E' : 'e') :
           (spec->type == FmtAuto ? (spec->uppercase ? 'G' : 'g') : 'a'));
  
  snprintf(format, sizeof(format), "%%%s%s%s%s%d.%dL%c",
    (spec->left_justify ? "-" : ""),
    (spec->show_sign ? "+" : (spec->space_prefix ? " " : "")),
    (spec->alternative_form ? "#" : ""),
    (spec->zero_pad ? "0" : ""),
    (spec->width > 0 ? spec->width : 0),
    (spec->precision >= 0 ? spec->precision : 6),
    type_char);
  
  snprintf(tmp, sizeof(tmp), format, value);
  
  size_t len = strlen(tmp);
  if (len >= buffer_size) {
    len = buffer_size - 1;
  }
  memcpy(buffer, tmp, len);
  buffer[len] = '\0';
}

void parse_format_specifier(const char** format, FormatSpecifier* spec, va_list args) {
  spec->width = -1;
  spec->precision = -1;
  spec->left_justify = false;
  spec->show_sign = false;
  spec->space_prefix = false;
  spec->alternative_form = false;
  spec->zero_pad = false;
  spec->uppercase = false;
  spec->length_modifier = '\0';
  spec->base = 10;

  while (**format) {
    switch (**format) {
      case '-': spec->left_justify = true; break;
      case '+': spec->show_sign = true; break;
      case ' ': spec->space_prefix = true; break;
      case '#': spec->alternative_form = true; break;
      case '0': spec->zero_pad = true; break;
      default: goto parse_width;
    }
    (*format)++;
  }
parse_width:
  if (**format == '*') {
    spec->width = va_arg(args, int);
    (*format)++;
  } else {
    spec->width = 0;
    while (isdigit(**format)) {
      spec->width = spec->width * 10 + (**format - '0');
      (*format)++;
    }
  }

  if (**format == '.') {
    (*format)++;
    if (**format == '*') {
      spec->precision = va_arg(args, int);
      (*format)++;
    } else {
      spec->precision = 0;
      while (isdigit(**format)) {
        spec->precision = spec->precision * 10 + (**format - '0');
        (*format)++;
      }
    }
  }

  switch (**format) {
    case 'h':
      spec->length_modifier = 'h';
      (*format)++;
      if (**format == 'h') {
        spec->length_modifier = 'H';
        (*format)++;
      }
      break;
    case 'l':
      spec->length_modifier = 'l';
      (*format)++;
      if (**format == 'l') {
        spec->length_modifier = 'L';
        (*format)++;
      }
      break;
    case 'L':
    case 'j':
    case 'z':
    case 't':
      spec->length_modifier = **format;
      (*format)++;
      break;
  }

  switch (**format) {
    case 'd':
    case 'i': spec->type = FmtInt; break;
    case 'u': spec->type = FmtUint; break;
    case 'f':
    case 'F': spec->type = FmtDouble; spec->uppercase = (**format == 'F'); break;
    case 'e':
    case 'E': spec->type = FmtExponent; spec->uppercase = (**format == 'E'); break;
    case 'g':
    case 'G': spec->type = FmtAuto; spec->uppercase = (**format == 'G'); break;
    case 'a':
    case 'A': spec->type = FmtHex; spec->uppercase = (**format == 'A'); break;
    case 'c': spec->type = FmtChar; break;
    case 's': spec->type = FmtRstring; break;
    case 'S': spec->type = FmtString; break;
    case 'p': spec->type = FmtPointer; break;
    case 'x': spec->type = FmtUint; spec->base = 16; break;
    case 'X': spec->type = FmtUint; spec->base = 16; spec->uppercase = true; break;
    case 'o': spec->type = FmtUint; spec->base = 8; break;
    case '%': spec->type = FmtPercent; break;
    default: spec->type = FmtPercent; (*format)--; break;
  }
  (*format)++;
}

string format_string(const char* format, va_list args) {
  StringBuilder sb = string_builder__new();
  FormatSpecifier spec;
  char buffer[512];

  va_list args_copy;
  va_copy(args_copy, args);

  while (*format) {
    if (*format != '%') {
      string_builder__append_char(&sb, *format++);
      continue;
    }

    format++;
    parse_format_specifier(&format, &spec, args_copy);
    
    switch (spec.type) {
      case FmtChar: {
        char c = (char)va_arg(args, int);
        string_builder__append_char(&sb, c);
        break;
      }
      case FmtInt:
      case FmtUint:
      case FmtLong:
      case FmtUlong:
      case FmtLongLong:
      case FmtUlongLong: {
        uintmax_t value;
        if (spec.type == FmtInt || spec.type == FmtLong || spec.type == FmtLongLong) {
          intmax_t signed_value;
          if (spec.length_modifier == 'H') signed_value = (signed char)va_arg(args, int);
          else if (spec.length_modifier == 'h') signed_value = (short)va_arg(args, int);
          else if (spec.length_modifier == 'l') signed_value = va_arg(args, long);
          else if (spec.length_modifier == 'L') signed_value = va_arg(args, long long);
          else if (spec.length_modifier == 'j') signed_value = va_arg(args, intmax_t);
          else if (spec.length_modifier == 'z') signed_value = va_arg(args, ssize_t);
          else if (spec.length_modifier == 't') signed_value = va_arg(args, ptrdiff_t);
          else signed_value = va_arg(args, int);
          value = (signed_value < 0) ? (uintmax_t)(-signed_value) : (uintmax_t)signed_value;
        } else {
          if (spec.length_modifier == 'H') value = (unsigned char)va_arg(args, unsigned int);
          else if (spec.length_modifier == 'h') value = (unsigned short)va_arg(args, unsigned int);
          else if (spec.length_modifier == 'l') value = va_arg(args, unsigned long);
          else if (spec.length_modifier == 'L') value = va_arg(args, unsigned long long);
          else if (spec.length_modifier == 'j') value = va_arg(args, uintmax_t);
          else if (spec.length_modifier == 'z') value = va_arg(args, size_t);
          else if (spec.length_modifier == 't') value = (uintmax_t)va_arg(args, ptrdiff_t);
          else value = va_arg(args, unsigned int);
        }
        format_integer(buffer, sizeof(buffer), value, &spec);
        string_builder__append_cstr(&sb, buffer);
        break;
      }
      case FmtDouble:
      case FmtExponent:
      case FmtAuto:
      case FmtHex: {
        long double value;
        if (spec.length_modifier == 'L') value = va_arg(args, long double);
        else value = va_arg(args, double);
        format_float(buffer, sizeof(buffer), value, &spec);
        string_builder__append_cstr(&sb, buffer);
        break;
      }
      case FmtRstring: {
        char* str = va_arg(args, char*);
        if (str) {
          size_t len = strlen(str);
          if (spec.precision >= 0 && (size_t)spec.precision < len) {
            len = (size_t)spec.precision;
          }
          string s = string__new(str);
          string s2 = string__substring(s, 0, (ssize_t)len);
          string_builder__append(&sb, s2);
          string__free(s);
          string__free(s2);
        } else {
          string_builder__append_cstr(&sb, "(null)");
        }
        break;
      }
      case FmtString: {
        string str = va_arg(args, string);
        if (spec.precision >= 0 && (size_t)spec.precision < str.len) {
          string s2 = string__substring(str, 0, (ssize_t)spec.precision);
          string_builder__append(&sb, s2);
          string__free(s2);
        } else {
          string_builder__append(&sb, str);
        }
        break;
      }
      case FmtPointer: {
        void* ptr = va_arg(args, void*);
        snprintf(buffer, sizeof(buffer), "%p", ptr);
        string_builder__append_cstr(&sb, buffer);
        break;
      }
      case FmtPercent: {
        string_builder__append_char(&sb, '%');
        break;
      }
    }
  }
  
  va_end(args_copy);
  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}