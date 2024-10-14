#ifndef __RICKSHELL_STRCONV_H__
#define __RICKSHELL_STRCONV_H__
#include <stddef.h>
#include "result.h"
#include "rstring.h"

typedef enum {
  STRCONV_SUCCESS = 0,
  STRCONV_CONVERSION_ERROR,
  STRCONV_OVERFLOW,
  STRCONV_UNDERFLOW,
  STRCONV_INVALID_INPUT
} StrconvErrCode;

StrconvResult ratoi(const string str, int* out);
StrconvResult ratol(const string str, long* out);
StrconvResult ratoll(const string str, long long* out);
StrconvResult ratof(const string str, float* out);
StrconvResult ratod(const string str, double* out);
StrconvResult ritoa(int value);
StrconvResult rltos(long value);
StrconvResult rlltos(long long value);
StrconvResult rftos(float value);
StrconvResult rdtos(double value);

typedef enum {
  FmtChar,
  FmtInt,
  FmtUint,
  FmtLong,
  FmtUlong,
  FmtLongLong,
  FmtUlongLong,
  FmtDouble,
  FmtExponent,
  FmtAuto,
  FmtHex,
  FmtString,
  FmtRstring,
  FmtPointer,
  FmtPercent
} FormatType;

typedef struct {
  FormatType type;
  int width;
  int precision;
  bool uppercase;
  bool left_justify;
  bool show_sign;
  bool space_prefix;
  bool alternative_form;
  bool zero_pad;
  char length_modifier;
  int base;
} FormatSpecifier;

void parse_format_specifier(const char** format, FormatSpecifier* spec, va_list args);
string format_string(const char* format, va_list args);
#endif /* __RICKSHELL_STRCONV_H__ */