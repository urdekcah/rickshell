#ifndef __RICKSHELL_UNICODE_H__
#define __RICKSHELL_UNICODE_H__
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "result.h"

UnicodeResult unicode_to_utf8(uint32_t codepoint, char* buffer, size_t buffer_size, size_t* bytes_written);
UnicodeResult utf8_to_unicode(const char* utf8_str, size_t str_len, uint32_t* codepoint, size_t* bytes_read);
bool is_valid_utf8(const char* str, size_t str_len);
UnicodeResult utf8_length(const char* str, size_t str_len, size_t* count);
#endif /* __RICKSHELL_UNICODE_H__ */