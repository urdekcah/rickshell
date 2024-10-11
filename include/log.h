#ifndef __RICKSHELL_LOG_H__
#define __RICKSHELL_LOG_H__
#include <stdarg.h>
#include <stdbool.h>
#include "rstring.h"

typedef enum {
  LOG_LEVEL_TRACE,
  LOG_LEVEL_DEBUG,
  LOG_LEVEL_INFO,
  LOG_LEVEL_WARN,
  LOG_LEVEL_ERROR,
  LOG_LEVEL_FATAL
} LogLevel;

typedef struct {
  const string name;
  LogLevel level;
  bool color_output;
  const string filename;
  unsigned long max_file_size;
  unsigned int max_backup_files;
  bool append_mode;
  bool file_output_only;
  const string log_format;
} LogConfig;

void log_init(const LogConfig* config);
void log_shutdown(void);
void log_set_level(LogLevel level);
void log_set_color_output(bool enabled);
void log_rotate(void);

#define log_trace(...) log_write(LOG_LEVEL_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_debug(...) log_write(LOG_LEVEL_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_info(...)  log_write(LOG_LEVEL_INFO,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...)  log_write(LOG_LEVEL_WARN,  __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_write(LOG_LEVEL_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_write(LOG_LEVEL_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)

void log_write(LogLevel level, const char* file, int line, const char* func, const char* format, ...);
#endif /* __RICKSHELL_LOG_H__ */