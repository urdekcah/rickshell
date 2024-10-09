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
  if (string__is_null_or_empty(str) || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long val = strtol(str.str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str.str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  if (val > INT_MAX || val < INT_MIN) {
    long ecode = (val > 0) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  *out = (int)val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratol(const string str, long* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long val = strtol(str.str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str.str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratoll(const string str, long long* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long long val = strtoll(str.str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LLONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str.str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratof(const string str, float* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  float val = strtof(str.str, &endptr);
  if (errno == ERANGE) {
    long ecode = (val == HUGE_VALF) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str.str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratod(const string str, double* out) {
  if (string__is_null_or_empty(str) || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  double val = strtod(str.str, &endptr);
  if (errno == ERANGE) {
    long ecode = (val == HUGE_VAL) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str.str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ritoa(int value, size_t size) {
  StringBuilder sb = string_builder__new();
  string_builder__append_int(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (s.len >= size)
    return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)&s);
}

StrconvResult rltos(long value, size_t size) {
  StringBuilder sb = string_builder__new();
  string_builder__append_long(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (s.len >= size)
    return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)&s);
}

StrconvResult rlltos(long long value, size_t size) {
  StringBuilder sb = string_builder__new();
  string_builder__append_long_long(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (s.len >= size)
    return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)&s);
}

StrconvResult rftos(float value, size_t size) {
  StringBuilder sb = string_builder__new();
  string_builder__append_float(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (s.len >= size)
    return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)&s);
}

StrconvResult rdtos(double value, size_t size) {
  StringBuilder sb = string_builder__new();
  string_builder__append_double(&sb, value);
  string s = string_builder__to_string(&sb);
  string_builder__free(&sb);
  if (s.len >= size)
    return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)&s);
}

static void format_integer(char* buffer, size_t buffer_size, uintmax_t value, FormatSpecifier* spec) {
  char tmp[65];
  int len = 0;
  const char* digits = (spec->type == FmtUint || spec->alternative_form) ? "0123456789ABCDEF" : "0123456789abcdef";

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

  if (spec->alternative_form && spec->base == 16) {
    tmp[len++] = 'x';
    tmp[len++] = '0';
  } else if (spec->alternative_form && spec->base == 8 && tmp[len-1] != '0') {
    tmp[len++] = '0';
  }

  size_t padding = (spec->width > len) ? (size_t)(spec->width - len) : 0;
  size_t total_len = (size_t)len + padding;

  if (total_len >= buffer_size) {
    total_len = buffer_size - 1;
    len = (int)(total_len - padding);
  }

  size_t index = 0;
  if (!spec->left_justify && spec->zero_pad) {
    for (size_t i = 0; i < padding; i++) {
      buffer[index++] = '0';
    }
  } else if (!spec->left_justify) {
    for (size_t i = 0; i < padding; i++) {
      buffer[index++] = ' ';
    }
  }

  for (int i = len - 1; i >= 0; i--) {
    buffer[index++] = tmp[i];
  }

  if (spec->left_justify) {
    for (size_t i = 0; i < padding; i++) {
      buffer[index++] = ' ';
    }
  }

  buffer[index] = '\0';
}

static void format_float(char* buffer, size_t buffer_size, long double value, FormatSpecifier* spec) {
  char format[32];
  char tmp[128];
  
  snprintf(format, sizeof(format), "%%%s%d.%dL%c",
       (spec->left_justify ? "-" : ""),
       (spec->width > 0 ? spec->width : 0),
       (spec->precision >= 0 ? spec->precision : 6),
       (spec->type == FmtDouble ? 'f' : 'g'));
  
  snprintf(tmp, sizeof(tmp), format, value);
  
  size_t len = strlen(tmp);
  if (len >= buffer_size) {
    len = buffer_size - 1;
  }
  memcpy(buffer, tmp, len);
  buffer[len] = '\0';
}

void parse_format_specifier(const char** format, FormatSpecifier* spec, va_list* args) {
  spec->width = -1;
  spec->precision = -1;
  spec->left_justify = false;
  spec->show_sign = false;
  spec->space_prefix = false;
  spec->alternative_form = false;
  spec->zero_pad = false;
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
    spec->width = va_arg(*args, int);
    (*format)++;
  } else {
    int width;
    string temp = string__new(*format);
    StrconvResult res = ratoi(temp, &width);
    string__free(temp);
    spec->width = (res.is_err) ? -1 : width;
  }

  if (**format == '.') {
    (*format)++;
    if (**format == '*') {
      spec->precision = va_arg(*args, int);
      (*format)++;
    } else {
      int precision;
      string temp = string__new(*format);
      StrconvResult res = ratoi(temp, &precision);
      string__free(temp);
      spec->precision = (res.is_err) ? -1 : precision;
    }
  }

  switch (**format) {
    case 'h':
    case 'l':
    case 'L':{
      spec->length_modifier = **format;
      (*format)++;
      if (spec->length_modifier == 'l' && **format == 'l') {
        spec->length_modifier = 'L';
        (*format)++;
      }
      break;
    }
  }

  switch (**format) {
    case 'd':
    case 'i': spec->type = FmtInt; break;
    case 'u': spec->type = FmtUint; break;
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'a':
    case 'A': spec->type = FmtDouble; break;
    case 'c': spec->type = FmtChar; break;
    case 's': spec->type = FmtRstring; break;
    case 'S': spec->type = FmtString; break;
    case 'p': spec->type = FmtPointer; break;
    case 'x':
    case 'X': spec->type = FmtUint; spec->base = 16; break;
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

  while (*format) {
    if (*format != '%') {
      string_builder__append_char(&sb, *format++);
      continue;
    }

    format++;
    parse_format_specifier((const char**)&format, &spec, (va_list*)&args);
    
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
          if (spec.length_modifier == 'h') signed_value = (short)va_arg(args, int);
          else if (spec.length_modifier == 'l') signed_value = va_arg(args, long);
          else if (spec.length_modifier == 'L') signed_value = va_arg(args, long long);
          else signed_value = va_arg(args, int);
          value = (signed_value < 0) ? (uintmax_t)(-signed_value) : (uintmax_t)signed_value;
        } else {
          if (spec.length_modifier == 'h') value = (unsigned short)va_arg(args, unsigned int);
          else if (spec.length_modifier == 'l') value = va_arg(args, unsigned long);
          else if (spec.length_modifier == 'L') value = va_arg(args, unsigned long long);
          else value = va_arg(args, unsigned int);
        }
        format_integer(buffer, sizeof(buffer), value, &spec);
        string_builder__append_cstr(&sb, buffer);
        break;
      }
      case FmtDouble: {
        long double value;
        if (spec.length_modifier == 'L') value = va_arg(args, long double);
        else value = va_arg(args, double);
        format_float(buffer, sizeof(buffer), value, &spec);
        string_builder__append_cstr(&sb, buffer);
        break;
      }
      case FmtRstring: {
        char* str = va_arg(args, char*);
        string_builder__append_cstr(&sb, str ? str : "(null)");
        break;
      }
      case FmtString: {
        string str = va_arg(args, string);
        string_builder__append(&sb, str);
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
  
  string result = string_builder__to_string(&sb);
  string_builder__free(&sb);
  return result;
}