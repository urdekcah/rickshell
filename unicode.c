#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "error.h"
#include "rstring.h"
#include "result.h"
#include "unicode.h"
#define UTF8_ACCEPT 0
#define UTF8_REJECT 1

static const uint8_t utf8d[] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 00..1f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 20..3f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 40..5f
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 60..7f
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9,9, // 80..9f
  7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7,7, // a0..bf
  8,8,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2, // c0..df
  0xa,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x3,0x4,0x3,0x3, // e0..ef
  0xb,0x6,0x6,0x6,0x5,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8,0x8, // f0..ff
  0x0,0x1,0x2,0x3,0x5,0x8,0x7,0x1,0x1,0x1,0x4,0x6,0x1,0x1,0x1,0x1, // s0..s0
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1,1,1,0,1,0,1,1,1,1,1,1, // s1..s2
  1,2,1,1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1, // s3..s4
  1,2,1,1,1,1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,1,1,3,1,3,1,1,1,1,1,1, // s5..s6
  1,3,1,1,1,1,1,3,1,3,1,1,1,1,1,1,1,3,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // s7..s8
};

static uint32_t decode(uint32_t* state, uint32_t* codep, uint32_t byte) {
  uint32_t type = utf8d[byte];

  *codep = (*state != UTF8_ACCEPT) ?
    (byte & 0x3fu) | (*codep << 6) :
    (0xff >> type) & (byte);

  *state = utf8d[256 + *state*16 + type];
  return *state;
}

UnicodeResult unicode_to_utf8(uint32_t codepoint, char* buffer, size_t buffer_size, size_t* bytes_written) {
  if (buffer == NULL || bytes_written == NULL) return Err(
    _SLIT("buffer or bytes_written is NULL"),
    ERRCODE_UNICODE_NULL_POINTER
  );
  if (codepoint > 0x10FFFF) return Err(
    _SLIT("Invalid codepoint"),
    ERRCODE_UNICODE_INVALID_CODEPOINT
  );

  if (codepoint <= 0x7F) {
    if (buffer_size < 1) return Err(
      _SLIT("Buffer too small"),
      ERRCODE_UNICODE_BUFFER_TOO_SMALL
    );
    buffer[0] = (char)codepoint;
    *bytes_written = 1;
  } else if (codepoint <= 0x7FF) {
    if (buffer_size < 2) return Err(
      _SLIT("Buffer too small"),
      ERRCODE_UNICODE_BUFFER_TOO_SMALL
    );
    buffer[0] = (char)(0xC0 | (codepoint >> 6));
    buffer[1] = (char)(0x80 | (codepoint & 0x3F));
    *bytes_written = 2;
  } else if (codepoint <= 0xFFFF) {
    if (buffer_size < 3) return Err(
      _SLIT("Buffer too small"),
      ERRCODE_UNICODE_BUFFER_TOO_SMALL
    );
    buffer[0] = (char)(0xE0 | (codepoint >> 12));
    buffer[1] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    buffer[2] = (char)(0x80 | (codepoint & 0x3F));
    *bytes_written = 3;
  } else {
    if (buffer_size < 4) return Err(
      _SLIT("Buffer too small"),
      ERRCODE_UNICODE_BUFFER_TOO_SMALL
    );
    buffer[0] = (char)(0xF0 | (codepoint >> 18));
    buffer[1] = (char)(0x80 | ((codepoint >> 12) & 0x3F));
    buffer[2] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
    buffer[3] = (char)(0x80 | (codepoint & 0x3F));
    *bytes_written = 4;
  }

  return Ok(NULL);
}

UnicodeResult utf8_to_unicode(const char* utf8_str, size_t str_len, uint32_t* codepoint, size_t* bytes_read) {
  if (utf8_str == NULL || codepoint == NULL || bytes_read == NULL) return Err(
    _SLIT("codepoint or bytes_read is NULL"),
    ERRCODE_UNICODE_NULL_POINTER
  );

  uint32_t state = 0;
  uint32_t codep = 0;
  size_t i;

  for (i = 0; i < str_len; i++) {
    if (decode(&state, &codep, (uint8_t)utf8_str[i]) == UTF8_REJECT) return Err(
      _SLIT("Invalid UTF-8"),
      ERRCODE_UNICODE_INVALID_UTF8
    );
    if (state == UTF8_ACCEPT) {
      *codepoint = codep;
      *bytes_read = i + 1;
      return Ok(NULL);
    }
  }
  
  if (state != UTF8_ACCEPT) return Err(
    _SLIT("Invalid UTF-8"),
    ERRCODE_UNICODE_INVALID_UTF8
  );
  return Ok(NULL);
}

bool is_valid_utf8(const char* str, size_t str_len) {
  if (str == NULL)
    return false;

  uint32_t state = 0;
  uint32_t codep = 0;

  for (size_t i = 0; i < str_len; i++)
    if (decode(&state, &codep, (uint8_t)str[i]) == UTF8_REJECT)
      return false;

  return state == UTF8_ACCEPT;
}

UnicodeResult utf8_length(const char* str, size_t str_len, size_t* count) {
  if (str == NULL || count == NULL) return Err(
    _SLIT("str or count is NULL"),
    ERRCODE_UNICODE_NULL_POINTER
  );
  *count = 0;
  uint32_t state = 0;
  uint32_t codep = 0;
  for (size_t i = 0; i < str_len; i++) {
    if (decode(&state, &codep, (uint8_t)str[i]) == UTF8_REJECT) return Err(
      _SLIT("Invalid UTF-8"),
      ERRCODE_UNICODE_INVALID_UTF8
    );
    if (state == UTF8_ACCEPT)
      (*count)++;
  }
  if (state != UTF8_ACCEPT) return Err(
    _SLIT("Invalid UTF-8"),
    ERRCODE_UNICODE_INVALID_UTF8
  );
  return Ok(NULL);
}