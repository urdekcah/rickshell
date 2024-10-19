#ifndef __RICKSHELL_ERROR_H__
#define __RICKSHELL_ERROR_H__
#include <stdio.h>
#include "rstring.h"

typedef enum {
  ERRCODE_ERR = 0,
  ERRCODE_MEMALLOC_FAILED,
  ERRCODE_INVALID_ARGUMENT,
  ERRCODE_INVALID_INPUT,
  /* Map Errors */
  ERRCODE_MAP_MEMORY,
  ERRCODE_MAP_KEY_NOT_FOUND,
  ERRCODE_MAP_KEY_EXISTS,
  ERRCODE_MAP_INVALID_ARGUMENT,
  /* Strconv Errors */
  ERRCODE_STRCONV_CONVERSION_FAILED,
  ERRCODE_STRCONV_OVERFLOW,
  ERRCODE_STRCONV_UNDERFLOW,
} ErrCode;

typedef struct {
  string msg;
  int code;
} ErrorInterface;

void print_error(const string message);
#endif /* __RICKSHELL_ERROR_H__ */