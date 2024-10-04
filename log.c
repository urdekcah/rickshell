#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include "file.h"
#include "memory.h"
#include "log.h"

#define MAX_LOG_MESSAGE_LENGTH 2048
#define DEFAULT_MAX_FILE_SIZE (10 * 1024 * 1024) // 10 MB
#define DEFAULT_MAX_BACKUP_FILES 5
#define DEFAULT_LOG_FORMAT "[%Y-%m-%d %H:%M:%S] [%L] [%p] (%a) %f:%l (%n): %m"

static const char* level_strings[] = {
  "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
};

static const char* level_colors[] = {
  "\x1b[94m", "\x1b[36m", "\x1b[32m", "\x1b[33m", "\x1b[31m", "\x1b[35m"
};

static struct {
  LogLevel level;
  FILE* file;
  char* filename;
  bool color_output;
  pthread_mutex_t mutex;
  unsigned long max_file_size;
  unsigned int max_backup_files;
  bool append_mode;
  bool file_output_only;
  char* log_format;
  char* app_name;
} log_ctx = {
  .level = LOG_LEVEL_INFO,
  .file = NULL,
  .filename = NULL,
  .color_output = false,
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .max_file_size = DEFAULT_MAX_FILE_SIZE,
  .max_backup_files = DEFAULT_MAX_BACKUP_FILES,
  .append_mode = true,
  .file_output_only = true,
  .log_format = NULL,
  .app_name = NULL
};

static void ensure_log_file_open(void) {
  if (log_ctx.file == NULL && log_ctx.filename != NULL) {
    log_ctx.file = fopen(log_ctx.filename, log_ctx.append_mode ? "a" : "w");
    if (log_ctx.file == NULL) {
      fprintf(stderr, "Failed to open log file: %s (Error: %s)\n", log_ctx.filename, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }
}

void log_init(const LogConfig* config) {
  if (config == NULL) return;

  log_ctx.level = config->level;
  log_ctx.color_output = config->color_output;
  log_ctx.max_file_size = config->max_file_size > 0 ? config->max_file_size : DEFAULT_MAX_FILE_SIZE;
  log_ctx.max_backup_files = config->max_backup_files > 0 ? config->max_backup_files : DEFAULT_MAX_BACKUP_FILES;
  log_ctx.append_mode = config->append_mode;
  log_ctx.file_output_only = config->file_output_only;

  if (config->filename != NULL) {
    log_ctx.filename = expand_home_directory(config->filename);
    ensure_log_file_open();
  }

  if (config->log_format != NULL) {
    log_ctx.log_format = strdup(config->log_format);
  } else {
    log_ctx.log_format = strdup(DEFAULT_LOG_FORMAT);
  }

  if (config->name != NULL) {
    log_ctx.app_name = strdup(config->name);
  }
}

void log_shutdown(void) {
  if (log_ctx.file != NULL) {
    fclose(log_ctx.file);
    log_ctx.file = NULL;
  }
  rfree(log_ctx.filename);
  log_ctx.filename = NULL;
  rfree(log_ctx.log_format);
  log_ctx.log_format = NULL;
  rfree(log_ctx.app_name);
  log_ctx.app_name = NULL;
}

void log_set_level(LogLevel level) {
  log_ctx.level = level;
}

void log_set_color_output(bool enabled) {
  log_ctx.color_output = enabled;
}

void log_rotate(void) {
  if (log_ctx.file == NULL || log_ctx.filename == NULL) return;

  fclose(log_ctx.file);
  log_ctx.file = NULL;

  for (long i = (long)log_ctx.max_backup_files - 1; i > 0; i--) {
    char old_name[256], new_name[256];
    snprintf(old_name, sizeof(old_name), "%s.%ld", log_ctx.filename, i);
    snprintf(new_name, sizeof(new_name), "%s.%ld", log_ctx.filename, i + 1);
    rename(old_name, new_name);
  }

  char backup_name[256];
  snprintf(backup_name, sizeof(backup_name), "%s.1", log_ctx.filename);
  rename(log_ctx.filename, backup_name);

  ensure_log_file_open();
}

static void check_file_size(void) {
  if (log_ctx.file == NULL || log_ctx.filename == NULL) return;

  struct stat st;
  if (stat(log_ctx.filename, &st) == 0) {
    if ((unsigned long long)st.st_size >= log_ctx.max_file_size) {
      log_rotate();
    }
  }
}

static void format_log_message(char* buffer, size_t buffer_size, const char* format, 
                               const char* level_str, const char* file, int line, 
                               const char* func, const char* message) {
  time_t now;
  time(&now);
  struct tm* time_info = localtime(&now);

  char* ptr = buffer;
  const char* end = buffer + buffer_size;

  for (const char* fmt = format; *fmt && ptr < end - 1; ++fmt) {
    if (*fmt == '%' && *(fmt + 1)) {
      fmt++;
      switch (*fmt) {
        case 'Y':
          ptr += strftime(ptr, (size_t)(end - ptr), "%Y", time_info);
          break;
        case 'M':
          ptr += strftime(ptr, (size_t)(end - ptr), "%m", time_info);
          break;
        case 'd':
          ptr += strftime(ptr, (size_t)(end - ptr), "%d", time_info);
          break;
        case 'H':
          ptr += strftime(ptr, (size_t)(end - ptr), "%H", time_info);
          break;
        case 'I':
          ptr += strftime(ptr, (size_t)(end - ptr), "%M", time_info);
          break;
        case 'S':
          ptr += strftime(ptr, (size_t)(end - ptr), "%S", time_info);
          break;
        case 'L':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%s", level_str);
          break;
        case 'p':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%d", getpid());
          break;
        case 'f':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%s", file);
          break;
        case 'l':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%d", line);
          break;
        case 'n':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%s", func);
          break;
        case 'a':
          if (log_ctx.app_name) {
            ptr += snprintf(ptr, (size_t)(end - ptr), "%s", log_ctx.app_name);
          }
          break;
        case 'm':
          ptr += snprintf(ptr, (size_t)(end - ptr), "%s", message);
          break;
        default:
          *ptr++ = '%';
          *ptr++ = *fmt;
      }
    } else {
      *ptr++ = *fmt;
    }
  }

  *ptr = '\0';
}

void log_write(LogLevel level, const char* file, int line, const char* func, const char* format, ...) {
  if (level < log_ctx.level) return;

  ensure_log_file_open();

  char message[MAX_LOG_MESSAGE_LENGTH];
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);

  char formatted_message[MAX_LOG_MESSAGE_LENGTH * 2];
  format_log_message(formatted_message, sizeof(formatted_message), log_ctx.log_format, 
             level_strings[level], file, line, func, message);

  pthread_mutex_lock(&log_ctx.mutex);

  check_file_size();

  if (log_ctx.file != NULL) {
    fprintf(log_ctx.file, "%s\n", formatted_message);
    fflush(log_ctx.file);
  }

  if (level >= LOG_LEVEL_ERROR || log_ctx.file == NULL) {
    if (log_ctx.color_output) {
      fprintf(stderr, "%s%s\x1b[0m\n", level_colors[level], formatted_message);
    } else {
      fprintf(stderr, "%s\n", formatted_message);
    }
  }

  pthread_mutex_unlock(&log_ctx.mutex);
}