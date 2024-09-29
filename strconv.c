#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <limits.h>
#include <errno.h>
#include "result.h"
#include "strconv.h"

StrconvResult ratoi(const char* str, int* out) {
  if (str == NULL || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long val = strtol(str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  if (val > INT_MAX || val < INT_MIN) {
    long ecode = (val > 0) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  *out = (int)val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratol(const char* str, long* out) {
  if (str == NULL || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long val = strtol(str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratoll(const char* str, long long* out) {
  if (str == NULL || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  long long val = strtoll(str, &endptr, 10);
  if (errno == ERANGE) {
    long ecode = (val == LLONG_MAX) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratof(const char* str, float* out) {
  if (str == NULL || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  float val = strtof(str, &endptr);
  if (errno == ERANGE) {
    long ecode = (val == HUGE_VALF) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ratod(const char* str, double* out) {
  if (str == NULL || out == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  char *endptr;
  errno = 0;
  double val = strtod(str, &endptr);
  if (errno == ERANGE) {
    long ecode = (val == HUGE_VAL) ? STRCONV_OVERFLOW : STRCONV_UNDERFLOW;
    return Err((void*)ecode);
  }
  if (endptr == str || *endptr != '\0') return Err((void*)STRCONV_INVALID_INPUT);
  *out = val;
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult ritoa(int value, char* str, size_t size) {
  if (str == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  int len = snprintf(str, size, "%d", value);
  if (len < 0 || (size_t)len >= size) return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult rltos(long value, char* str, size_t size) {
  if (str == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  int len = snprintf(str, size, "%ld", value);
  if (len < 0 || (size_t)len >= size) return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult rlltos(long long value, char* str, size_t size) {
  if (str == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  int len = snprintf(str, size, "%lld", value);
  if (len < 0 || (size_t)len >= size) return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult rftos(float value, char* str, size_t size) {
  if (str == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  int len = snprintf(str, size, "%f", value);
  if (len < 0 || (size_t)len >= size) return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)STRCONV_SUCCESS);
}

StrconvResult rdtos(double value, char* str, size_t size) {
  if (str == NULL) return Err((void*)STRCONV_INVALID_INPUT);
  int len = snprintf(str, size, "%f", value);
  if (len < 0 || (size_t)len >= size) return Err((void*)STRCONV_OVERFLOW);
  return Ok((void*)STRCONV_SUCCESS);
}