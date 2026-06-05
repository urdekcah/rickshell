#ifndef __RICKSHELL_IO_H__
#define __RICKSHELL_IO_H__
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "rstring.h"
#include "result.h"

/**
 * @brief Set by get_input() when the line reader reaches end of input.
 *
 * The line editor reads through its own descriptor, so feof(stdin) is not a
 * reliable end-of-input signal; this flag is. The main loop checks it to decide
 * when to exit. False until the first end-of-input read.
 */
extern bool input_eof;

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

string get_prompt(void);
void enable_raw_mode(void);
void disable_raw_mode(void);
void rick__redisplay_function(void);
string get_input(void);
#endif /* __RICKSHELL_IO_H__ */