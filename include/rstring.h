#ifndef __RICKSHELL_RSTRING_H__
#define __RICKSHELL_RSTRING_H__
#include <stdbool.h>
#include <stdarg.h>
#include <unistd.h>
#include "args.h"
#include "array.h"

typedef struct {
  char* str;
  size_t len;
  int is_lit;
} string;

#define _SLIT(s) ((string){.str = ("" s), .len = (sizeof(s) - 1), .is_lit = 1})
#define _SLIT0 ((string){.str = "", .len = 0, .is_lit = 1})

string string__new(const char* s);
string string__from(string s);
char* string__to_cstr(string s);
size_t string__length(string s);
string string__upper(string s);
string string__lower(string s);
string string__trim(string s);
string string__ltrim(string s);
string string__rtrim(string s);
string string__substring2(string s, ssize_t start);
string string__substring3(string s, ssize_t start, ssize_t end);
#define string__substring(...) GLUE(string__substring, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
string string__replace(string s, string old, string new);
string string__replace_all(string s, string old, string new);
string string__concat(string s1, string s2);
string string__concat_many(int count, ...);
bool string__contains(string s, string value);
bool string__startswith(string s, string prefix);
bool string__endswith(string s, string suffix);
ssize_t string__indexof(string s, string substr);
ssize_t string__lastindexof(string s, string substr);
ssize_t string__indexof_any(string s, string substrs[]);
ssize_t string__lastindexof_any(string s, string substrs[]);
string string__remove2(string s, size_t start);
string string__remove3(string s, size_t start, size_t end);
#define string__remove(...) GLUE(string__remove, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
StringArray string__split(string s, string delim);
string string__join(StringArray strings, string delim);
int string__compare(string s1, string s2);
bool string__equals(string s1, string s2);
string string__ljust(string s, size_t width, char pad);
string string__rjust(string s, size_t width, char pad);
bool string__is_null_or_empty(string s);
bool string__is_null_or_whitespace(string s);
string string__zfill(string s, size_t width);
string string__reverse(string s);
string string__repeat(string s, size_t count);
string string__capitalize(string s);
string string__swapcase(string s);
string string__remove_prefix(string s, string prefix, bool is_longest_match);
string string__remove_suffix(string s, string suffix, bool greedy);
string string__remove_quotes(string s);
bool string__isdigit(string s);
bool string__isdecimal(string s);
bool string__isalpha(string s);
bool string__isalnum(string s);
bool string__isprintable(string s);
bool string__isupper(string s);
bool string__islower(string s);
bool string__istitle(string s);
bool string__isidentifier(string s);
char string__min(string s);
char string__max(string s);
void rstring__upper(string s);
void rstring__lower(string s);
void rstring__trim(string s);
void string__free(string s);

typedef struct {
  char* buffer;
  size_t len;
  size_t capacity;
} StringBuilder;

StringBuilder string_builder__new();
StringBuilder string_builder__with_capacity(size_t capacity);
StringBuilder string_builder__from_string(string s);
void string_builder__append(StringBuilder* sb, string s);
void string_builder__append_cstr(StringBuilder* sb, const char* s);
void string_builder__append_char(StringBuilder* sb, char c);
void string_builder__append_short(StringBuilder* sb, short value);
void string_builder__append_int(StringBuilder* sb, int value);
void string_builder__append_long(StringBuilder* sb, long value);
void string_builder__append_long_long(StringBuilder* sb, long long value);
void string_builder__append_float(StringBuilder* sb, float value);
void string_builder__append_double(StringBuilder* sb, double value);
void string_builder__append_long_double(StringBuilder* sb, long double value);
void string_builder__insert(StringBuilder* sb, size_t index, string s);
void string_builder__insert_cstr(StringBuilder* sb, size_t index, const char* s);
void string_builder__insert_char(StringBuilder* sb, size_t index, char c);
void string_builder__remove3(StringBuilder* sb, size_t start, size_t end);
void string_builder__remove2(StringBuilder* sb, size_t start);
#define string_builder__remove(...) GLUE(string_builder__remove, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
bool string_builder__equals(StringBuilder* sb, StringBuilder* other);
string string_builder__to_string(StringBuilder* sb);
void string_builder__clear(StringBuilder* sb);
void string_builder__free(StringBuilder* sb);
#endif /* __RICKSHELL_RSTRING_H__ */