#ifndef __RICKSHELL_FILE_H__
#define __RICKSHELL_FILE_H__
#define MAX_PATH_DIRS 256

char* expand_home_directory(const char* path);
int ensure_directory_exist(const char *dir_path);
int ensure_file_exist(const char *file_path);
void parse_path();
#endif /* __RICKSHELL_FILE_H__ */