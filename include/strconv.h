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

typedef enum {
  FmtChar,
  FmtInt,
  FmtUint,
  FmtLong,
  FmtUlong,
  FmtLongLong,
  FmtUlongLong,
  FmtDouble,
  FmtString,
  FmtRstring,
  FmtPointer,
  FmtPercent
} FormatType;

typedef struct {
  FormatType type;
  int width;
  int precision;
  bool left_justify;
  bool show_sign;
  bool space_prefix;
  bool alternative_form;
  bool zero_pad;
  char length_modifier;
  int base;
} FormatSpecifier;

void parse_format_specifier(const char** format, FormatSpecifier* spec, va_list* args);
string format_string(const char* format, va_list args);
#endif /* __RICKSHELL_STRCONV_H__ */