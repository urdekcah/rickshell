#ifndef __RICKSHELL_RESULT_H__
#define __RICKSHELL_RESULT_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "args.h"

typedef struct {
  bool is_err;
  void* value; // Err or Real Value
  bool need_free;
} Result;

#define CONCAT(a, b) a##b

#define Err1(x) ((Result){.is_err = true, .value = x, .need_free = false})
#define Err2(x, need_free) ((Result){.is_err = true, .value = x, .need_free = need_free})
#define Ok1(x) ((Result){.is_err = false, .value = x, .need_free = false})
#define Ok2(x, need_free) ((Result){.is_err = false, .value = x, .need_free = need_free})

#define Err(...) GLUE(Err, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
#define Ok(...) GLUE(Ok, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)

typedef Result MapResult;
typedef Result StrconvResult;
#endif /* __RICKSHELL_RESULT_H__ */