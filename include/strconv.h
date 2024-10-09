#ifndef __RICKSHELL_STRCONV_H__
#define __RICKSHELL_STRCONV_H__
#include <stddef.h>
#include "result.h"
#include "rstring.h"

typedef enum {
  STRCONV_SUCCESS = 0,
  STRCONV_OVERFLOW,
  STRCONV_UNDERFLOW,
  STRCONV_INVALID_INPUT
} StrconvErrCode;

StrconvResult ratoi(const string str, int* out);
StrconvResult ratol(const string str, long* out);
StrconvResult ratoll(const string str, long long* out);
StrconvResult ratof(const string str, float* out);
StrconvResult ratod(const string str, double* out);
StrconvResult ritoa(int value, size_t size);
StrconvResult rltos(long value, size_t size);
StrconvResult rlltos(long long value, size_t size);
StrconvResult rftos(float value, size_t size);
StrconvResult rdtos(double value, size_t size);
#endif