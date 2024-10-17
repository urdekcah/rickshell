#ifndef __RICKSHELL_UNICODE_H__
#define __RICKSHELL_UNICODE_H__
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
typedef enum {
  UNICODE_SUCCESS = 0,
  UNICODE_ERROR_INVALID_CODEPOINT,
  UNICODE_ERROR_BUFFER_TOO_SMALL,
  UNICODE_ERROR_INVALID_UTF8,
  UNICODE_ERROR_NULL_POINTER
} UnicodeError;

UnicodeError unicode_to_utf8(uint32_t codepoint, char* buffer, size_t buffer_size, size_t* bytes_written);
UnicodeError utf8_to_unicode(const char* utf8_str, size_t str_len, uint32_t* codepoint, size_t* bytes_read);
bool is_valid_utf8(const char* str, size_t str_len);
UnicodeError utf8_length(const char* str, size_t str_len, size_t* count);
#endif /* __RICKSHELL_UNICODE_H__ */