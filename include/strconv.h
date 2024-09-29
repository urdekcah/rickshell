#ifndef __RICKSHELL_STRCONV_H__
#define __RICKSHELL_STRCONV_H__
#include <stddef.h>
#include "result.h"

typedef enum {
  STRCONV_SUCCESS = 0,
  STRCONV_OVERFLOW,
  STRCONV_UNDERFLOW,
  STRCONV_INVALID_INPUT
} StrconvErrCode;

StrconvResult ratoi(const char* str, int* out);
StrconvResult ratol(const char* str, long* out);
StrconvResult ratoll(const char* str, long long* out);
StrconvResult ratof(const char* str, float* out);
StrconvResult ratod(const char* str, double* out);
StrconvResult ritoa(int value, char* str, size_t size);
StrconvResult rltos(long value, char* str, size_t size);
StrconvResult rlltos(long long value, char* str, size_t size);
StrconvResult rftos(float value, char* str, size_t size);
StrconvResult rdtos(double value, char* str, size_t size);
#endif