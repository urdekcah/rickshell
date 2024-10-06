#ifndef __RICKSHELL_IO_H__
#define __RICKSHELL_IO_H__
#include <stdint.h>
#include <stddef.h>
#include "rstring.h"

ssize_t _write_to_fd(int fd, string s);
ssize_t _writeln_to_fd(int fd, string s);
void print(string s);
void println(string s);
#endif /* __RICKSHELL_IO_H__ */