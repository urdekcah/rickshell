#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <unistd.h>
#include <errno.h>
#include "rstring.h"
#include "memory.h"
#include "io.h"

ssize_t _write(int fd, const char *buf, size_t count) {
  if (count <= 0) return 0;
  const char *ptr = (const char *)buf;
  ssize_t remaining = (ssize_t)count;
  ssize_t written = 0;

  while (remaining > 0) {
    ssize_t n = write(fd, ptr, (size_t)remaining);
    if (n == -1) {
      if (errno == EINTR) continue;
      return -1;
    }
    if (n == 0) break;
    ptr += n;
    remaining -= n;
    written += n;
  }
  return written;
}

ssize_t _write_to_fd(int fd, string s) {
  size_t buf_len = string__length(s);
  if (buf_len == 0) return 0;
  FILE *stream = (fd == STDOUT_FILENO) ? stdout : 
                 (fd == STDERR_FILENO) ? stderr : 
                 fdopen(fd, "w");
  if (stream == NULL) {
    fprintf(stderr, "Failed to open stream: %s\n", strerror(errno));
    return -1;
  }
  size_t total_written = 0;
  while (total_written < buf_len) {
    size_t chunk_size = (buf_len - total_written > BUFSIZ) ? BUFSIZ : (buf_len - total_written);
    size_t written = fwrite(s.str + total_written, 1, chunk_size, stream);
    if (written == 0) {
      if (ferror(stream)) {
        fprintf(stderr, "Write error: %s\n", strerror(errno));
        return -1;
      }
      break;
    }
    total_written += written;
  }
  if (fflush(stream) != 0) {
    fprintf(stderr, "Failed to flush stream: %s\n", strerror(errno));
    return -1;
  }
  return (ssize_t)total_written;
}

ssize_t _writeln_to_fd(int fd, string s) {
  size_t buf_len = string__length(s) + 1;
  if (buf_len <= 1) return _write_to_fd(fd, _SLIT("\n"));
  char* buf = NULL;
  buf = (char*)rmalloc(buf_len);
  memcpy(buf, s.str, s.len);
  buf[s.len] = '\n';
  ssize_t written = _write_to_fd(fd, (string){.str = buf, .len = buf_len, .is_lit = 0});
  rfree(buf);
  return written;
}

void print(string s) {
  fflush(stdout);
  fflush(stderr);
  _write_to_fd(STDOUT_FILENO, s);
  fflush(stdout);
}

void println(string s) {
  fflush(stdout);
  fflush(stderr);
  _writeln_to_fd(STDOUT_FILENO, s);
  fflush(stdout);
}