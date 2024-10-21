#ifndef __RICKSHELL_RESULT_H__
#define __RICKSHELL_RESULT_H__
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include "args.h"
#include "error.h"

typedef struct {
  bool is_err;
  union {
    ErrorInterface err;
    void* value;
  };
  bool need_free;
} Result;

void report_error(const Result r);

#define CONCAT(a, b) a##b

#define Err1(x) ((Result){.is_err = true, .err = (ErrorInterface){ .msg = x, .code = ERRCODE_ERR }, .need_free = false})
#define Err2(x, c) ((Result){.is_err = true, .err = (ErrorInterface){ .msg = x, .code = c }, .need_free = false})
#define Ok1(x) ((Result){.is_err = false, .value = x, .need_free = false})
#define Ok2(x, c) ((Result){.is_err = false, .value = x, .need_free = c})

#define Err(...) GLUE(Err, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
#define Ok(...) GLUE(Ok, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)

#define UNWRAP(result, type) ({ \
  Result __r = (result); \
  if (__r.type == RESULT_ERROR) { \
    fprintf(stderr, "Unwrap failed: %s\n", __r.value.err.msg); \
    exit(1); \
  } \
  (type)__r.value.ok; \
})

#define TRY(v, result, type) ({ \
  Result __r = (result); \
  if (__r.is_err) { \
    return (Result)__r; \
  } \
  v = (type)__r.value.ok; \
})

#define NTRY(result) ({ \
  Result __r = (result); \
  if (__r.is_err) { \
    return (Result)__r; \
  } \
})

typedef Result IntResult;
typedef Result VariableResult;
typedef Result StringResult;
typedef Result MapResult;
typedef Result StrconvResult;
typedef Result UnicodeResult;
#endif /* __RICKSHELL_RESULT_H__ */