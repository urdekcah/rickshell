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
#include "rstring.h"
#include "io.h"
#include "strconv.h"

#define MAX_LOG_MESSAGE_LENGTH 2048
#define DEFAULT_MAX_FILE_SIZE (10 * 1024 * 1024) // 10 MB
#define DEFAULT_MAX_BACKUP_FILES 5
#define DEFAULT_LOG_FORMAT _SLIT("[%Y-%m-%d %H:%M:%S] [%L] [%p] (%a) %f:%l (%n): %m")

static const string level_strings[] = {
  _SLIT("TRACE"), _SLIT("DEBUG"), _SLIT("INFO"), _SLIT("WARN"), _SLIT("ERROR"), _SLIT("FATAL")
};

static const string level_colors[] = {
  _SLIT("\x1b[94m"), _SLIT("\x1b[36m"), _SLIT("\x1b[32m"), _SLIT("\x1b[33m"), _SLIT("\x1b[31m"), _SLIT("\x1b[35m")
};

static struct {
  LogLevel level;
  FILE* file;
  string filename;
  bool color_output;
  pthread_mutex_t mutex;
  unsigned long max_file_size;
  unsigned int max_backup_files;
  bool append_mode;
  bool file_output_only;
  string log_format;
  string app_name;
} log_ctx = {
  .level = LOG_LEVEL_INFO,
  .file = NULL,
  .filename = _SLIT0,
  .color_output = false,
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .max_file_size = DEFAULT_MAX_FILE_SIZE,
  .max_backup_files = DEFAULT_MAX_BACKUP_FILES,
  .append_mode = true,
  .file_output_only = true,
  .log_format = _SLIT0,
  .app_name = _SLIT0
};

static void ensure_log_file_open(void) {
  if (log_ctx.file == NULL && !string__is_null_or_empty(log_ctx.filename)) {
    log_ctx.file = fopen(log_ctx.filename.str, log_ctx.append_mode ? "a" : "w");
    if (log_ctx.file == NULL) {
      ffprintln(stderr, "Failed to open log file: %S (Error: %s)", log_ctx.filename, strerror(errno));
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

  if (!string__is_null_or_empty(config->filename)) {
    char* fname = expand_home_directory(config->filename.str);
    log_ctx.filename = string__new(fname);
    ensure_log_file_open();
    free(fname);
  }

  log_ctx.log_format = string__from((!string__is_null_or_empty(config->log_format))? config->log_format : DEFAULT_LOG_FORMAT);

  if (!string__is_null_or_empty(config->name))
    log_ctx.app_name = string__from(config->name);
}

void log_shutdown(void) {
  if (log_ctx.file != NULL) {
    fclose(log_ctx.file);
    log_ctx.file = NULL;
  }
  string__free(log_ctx.filename);
  log_ctx.filename = _SLIT0;
  string__free(log_ctx.log_format);
  log_ctx.log_format = _SLIT0;
  string__free(log_ctx.app_name);
  log_ctx.app_name = _SLIT0;
}

void log_set_level(LogLevel level) {
  log_ctx.level = level;
}

void log_set_color_output(bool enabled) {
  log_ctx.color_output = enabled;
}

void log_rotate(void) {
  if (log_ctx.file == NULL || string__is_null_or_empty(log_ctx.filename)) return;

  fclose(log_ctx.file);
  log_ctx.file = NULL;

  for (long i = (long)log_ctx.max_backup_files - 1; i > 0; i--) {
    StrconvResult olv = rltos(i);
    if (olv.is_err) continue;
    StrconvResult nlv = rltos(i + 1);
    if (nlv.is_err) {
      string__free(*((string*)olv.value));
      continue;
    }
    string old_name = string__concat_many(3, log_ctx.filename, _SLIT("."), olv);
    string new_name = string__concat_many(3, log_ctx.filename, _SLIT("."), nlv);
    string__free(*((string*)olv.value));
    string__free(*((string*)nlv.value));
    rename(old_name.str, new_name.str);
    string__free(old_name);
    string__free(new_name);
  }

  string backup_name = string__concat(log_ctx.filename, _SLIT(".1"));
  rename(log_ctx.filename.str, backup_name.str);
  string__free(backup_name);

  ensure_log_file_open();
}

static void check_file_size(void) {
  if (log_ctx.file == NULL || string__is_null_or_empty(log_ctx.filename)) return;

  struct stat st;
  if (stat(log_ctx.filename.str, &st) == 0) {
    if ((unsigned long long)st.st_size >= log_ctx.max_file_size) {
      log_rotate();
    }
  }
}

static void format_log_message(string *buffer, const string format, 
                               const string level_str, const string file, int line, 
                               const string func, const string message) {
  time_t now;
  time(&now);
  struct tm* time_info = localtime(&now);

  StringBuilder sb = string_builder__new();

  for (size_t i = 0; i < format.len; i++) {
    if (format.str[i] == '%' && i + 1 < format.len) {
      i++;
      char time_buffer[32];
      switch (format.str[i]) {
        case 'Y':
          strftime(time_buffer, sizeof(time_buffer), "%Y", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'M':
          strftime(time_buffer, sizeof(time_buffer), "%m", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'd':
          strftime(time_buffer, sizeof(time_buffer), "%d", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'H':
          strftime(time_buffer, sizeof(time_buffer), "%H", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'I':
          strftime(time_buffer, sizeof(time_buffer), "%M", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'S':
          strftime(time_buffer, sizeof(time_buffer), "%S", time_info);
          string_builder__append_cstr(&sb, time_buffer);
          break;
        case 'L':string_builder__append(&sb, level_str);
          break;
        case 'p':
          string_builder__append_int(&sb, getpid());
          break;
        case 'f':string_builder__append(&sb, file);
          break;
        case 'l':
          string_builder__append_int(&sb, line);
          break;
        case 'n':
          string_builder__append(&sb, func);
          break;
        case 'a':
          if (!string__is_null_or_empty(log_ctx.app_name))
            string_builder__append(&sb, log_ctx.app_name);
          break;
        case 'm':
          string_builder__append(&sb, message);
          break;
        default:
          string_builder__append_char(&sb, '%');
          string_builder__append_char(&sb, format.str[i]);
      }
    } else {
      string_builder__append_char(&sb, format.str[i]);
    }
  }

  *buffer = string_builder__to_string(&sb);
  string_builder__free(&sb);
}

void log_write(LogLevel level, const char* file, int line, const char* func, const char* format, ...) {
  if (level < log_ctx.level) return;

  ensure_log_file_open();

  va_list args;
  va_start(args, format);
  char message_cstr[MAX_LOG_MESSAGE_LENGTH];
  vsnprintf(message_cstr, sizeof(message_cstr), format, args);
  va_end(args);

  string message = string__new(message_cstr);
  string sfile = string__new(file);
  string sfunc = string__new(func);
  string formatted_message;
  format_log_message(&formatted_message, log_ctx.log_format, 
             level_strings[level], sfile, line, sfunc, message);
  string__free(sfile);
  string__free(sfunc);

  pthread_mutex_lock(&log_ctx.mutex);

  check_file_size();

  if (log_ctx.file != NULL) {
    ffprintln(log_ctx.file, "%S", formatted_message);
    fflush(log_ctx.file);
  }

  if (level >= LOG_LEVEL_ERROR || log_ctx.file == NULL) {
    if (log_ctx.color_output) {
      ffprintln(stderr, "%S%S\x1b[0m", level_colors[level], formatted_message);
    } else {
      ffprintln(stderr, "%S", formatted_message);
    }
  }

  pthread_mutex_unlock(&log_ctx.mutex);

  string__free(message);
  string__free(formatted_message);
}