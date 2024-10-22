#ifndef __RICKSHELL_IO_H__
#define __RICKSHELL_IO_H__
#include <stdint.h>
#include <stddef.h>
#include "rstring.h"
#include "result.h"

ssize_t _write_to_fd(int fd, string s);
ssize_t _writeln_to_fd(int fd, string s);
void print(string s);
void println(string s);
void fprint(const char* format, ...);
void fprintln(const char* format, ...);
void ffprint(FILE* __stream, const char* format, ...);
void ffprintln(FILE* __stream, const char* format, ...);

typedef struct {
  char *buf;
  size_t capacity;
  size_t len;
  size_t cursor_pos;
} InputBuffer;

void print_prompt(void);
void enable_raw_mode(void);
void disable_raw_mode(void);
StringResult get_input(string* result);
#endif /* __RICKSHELL_IO_H__ */