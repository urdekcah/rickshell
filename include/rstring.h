#ifndef __RICKSHELL_RSTRING_H__
#define __RICKSHELL_RSTRING_H__
#include <stdbool.h>
#include <stdarg.h>
#include "args.h"
#include "array.h"

typedef struct {
  char* str;
  size_t len;
  int is_lit;
} string;

string string__new(const char* s);
size_t string__length(string s);
string string__upper(string s);
string string__lower(string s);
string string__trim(string s);
string string__ltrim(string s);
string string__rtrim(string s);
string string__substring2(string s, size_t start);
string string__substring3(string s, size_t start, size_t end);
#define string__substring(...) GLUE(string__substring, VAR_COUNT(__VA_ARGS__))(__VA_ARGS__)
string string__replace(string s, string old, string new);
string string__replace_all(string s, string old, string new);
string string__concat(string s1, string s2);
string string__concat_many(int count, ...);
bool string__contains(string s, string value);
bool string__startswith(string s, string prefix);
bool string__endswith(string s, string suffix);
size_t string__indexof(string s, string substr);
size_t string__lastindexof(string s, string substr);
size_t string__indexof_any(string s, string substrs[]);
size_t string__lastindexof_any(string s, string substrs[]);
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
void string__free(string s);

char* remove_quotes(const char* str);
char* str_replace(const char* src, const char* old, const char* new, bool replace_all);
bool match_pattern(const char* str, const char* pattern);
char* remove_prefix(const char* value, const char* pattern, bool is_longest_match);
char* remove_suffix(const char* str, const char* suffix, bool greedy);
char* dynstrcpy(char** dest, size_t* dest_size, size_t* dest_len, const char* src);
void ltrim(char* str);
void rtrim(char* str);
void trim(char* str);
void strupper(char* str);
void strlower(char* str);
#endif /* __RICKSHELL_RSTRING_H__ */