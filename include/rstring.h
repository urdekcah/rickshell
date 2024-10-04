#ifndef __RICKSHELL_RSTRING_H__
#define __RICKSHELL_RSTRING_H__
#include <stdbool.h>

char* remove_quotes(const char* str);
char* str_replace(const char* src, const char* old, const char* new, bool replace_all);
bool match_pattern(const char* str, const char* pattern);
char* remove_prefix(const char* value, const char* pattern, bool is_longest_match);
char* remove_suffix(const char* str, const char* suffix, bool greedy);
char* dynstrcpy(char** dest, size_t* dest_size, size_t* dest_len, const char* src);
void ltrim(char* str);
void rtrim(char* str);
void trim(char* str);
#endif /* __RICKSHELL_RSTRING_H__ */