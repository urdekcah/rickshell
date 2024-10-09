#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include "result.h"
#include "rstring.h"
#include "strconv.h"

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