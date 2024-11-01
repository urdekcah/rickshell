#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <readline/readline.h>
#include <readline/history.h>
#include <pwd.h>
#include <unistd.h>
#include "expr.h"
#include "array.h"
#include "history.h"
#include "rstring.h"
#include "strconv.h"
#include "file.h"

#define MAX_TIME_STR_LEN 128
#define MAX_HISTORY_LINE 8192

bool do_not_save_history = false;

static HistoryFlags parse_flags(const string flag_str) {
  HistoryFlags flags = {0};
  flags.delete_offset = -1;

  for (size_t i = 0; i < flag_str.len; i++) {
    switch (flag_str.str[i]) {
      case 'c': flags.clear = true; break;
      case 'd': flags.delete_entry = true; break;
      case 'a': flags.append = true; break;
      case 'n': flags.read_new = true; break;
      case 'r': flags.read_file = true; break;
      case 'w': flags.write = true; break;
      case 'p': flags.print_expand = true; break;
      case 's': flags.store = true; break;
      default:
        fprintf(stderr, "history: invalid option -- '%c'\n", flag_str.str[i]);
        break;
    }
  }
  return flags;
}

static bool handle_file_operation(const char* operation, history_op_t func) {
  const char* hist_env = getenv("HISTFILE");
  const char* path = hist_env ? hist_env : DEFAULT_HISTFILE;
  char* filename = expand_home_directory(path);
  if (!filename) {
    fprintf(stderr, "Failed to %s: Could not get history file path\n", operation);
    return false;
  }

  if (func(filename) != 0) {
    fprintf(stderr, "Failed to %s: %s\n", operation, strerror(errno));
    free(filename);
    return false;
  }

  free(filename);
  return true;
}

static void print_history_entry(int index, const char* time_format, const HIST_ENTRY* entry) {
  if (!entry || !entry->line) {
    return;
  }

  if (time_format) {
    char time_str[MAX_TIME_STR_LEN];
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    
    if (tm_info && strftime(time_str, sizeof(time_str), time_format, tm_info) > 0) {
      printf("%5d  %s  %s\n", index + 1, time_str, entry->line);
    } else {
      fprintf(stderr, "history: Failed to format time\n");
      printf("%5d  %s\n", index + 1, entry->line);
    }
  } else {
    printf("%5d  %s\n", index + 1, entry->line);
  }
}

static bool store_history_entry(const Command* cmd, size_t start_idx) {
  if (!cmd) return false;
  
  StringBuilder sb = string_builder__new();
  bool in_quotes = false;
  
  for (size_t i = start_idx; i < cmd->argv.size; i++) {
    string arg = *(string*)array_checked_get(cmd->argv, i);

    if (i > start_idx && !in_quotes)
      string_builder__append_char(&sb, ' ');
    
    if (arg.str[0] == '"' || arg.str[0] == '\'') {
      in_quotes = true;
      string content = string__substring(arg, 1);
      string_builder__append(&sb, content);
      string__free(content);
    } else if (arg.len > 0 && (arg.str[arg.len-1] == '"' || arg.str[arg.len-1] == '\'')) {
      in_quotes = false;
      string content = string__remove_quotes(arg);
      string_builder__append(&sb, content);
      string__free(content);
    } else {
      string_builder__append(&sb, arg);
    }
  }

  string content = string_builder__to_string(&sb);
  if (content.str) {
    if (content.len > MAX_HISTORY_LINE) {
      fprintf(stderr, "history: entry too long\n");
      string__free(content);
      string_builder__free(&sb);
      return false;
    }
    add_history(content.str);
    string__free(content);
    do_not_save_history = true;
    reset_last_command();
  }
  
  string_builder__free(&sb);
  return true;
}

static bool delete_history_entry(int offset, int hist_len) {
  int internal_offset = offset - 1;
  if (offset < 0) offset = hist_len + offset;
  
  if (!(offset >= 0 && offset < hist_len)) {
    fprintf(stderr, "history: %d: invalid history position\n", offset);
    return false;
  }
  
  HIST_ENTRY* entry = remove_history(internal_offset);
  if (entry) {
    free_history_entry(entry);
    return true;
  }
  
  fprintf(stderr, "history: Failed to delete entry %d\n", offset);
  return false;
}

static bool handle_print_expand(const Command* cmd, size_t start_idx) {
  for (size_t i = start_idx; i < cmd->argv.size; i++) {
    string arg = *(string*)array_checked_get(cmd->argv, i);
    if (!arg.str) continue;
    
    // todo!(): need expand history
    printf("%s\n", arg.str);
  }
  return true;
}


static int append_history_wrapper(const char* filename) {
  return append_history(history_length, filename);
}

int builtin_history(Command* cmd) {
  if (!cmd || cmd->argv.size < 1)
    return 1;

  char* time_format = getenv("HISTTIMEFORMAT");
  HIST_ENTRY** hist_list = history_list();
  int hist_len = history_length;
  
  if (!hist_list && hist_len > 0) {
    fprintf(stderr, "history: Failed to get history list\n");
    return 1;
  }

  HistoryFlags flags = {0};
  bool waiting_for_offset = false;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string arg = *(string*)array_checked_get(cmd->argv, i);
    if (!arg.str) continue;

    if (waiting_for_offset) {
      if (string__isdigit(arg)) {
        int offset;
        if (!ratoi(arg, &offset).is_err)
          return delete_history_entry(offset, hist_len) ? 0 : 1;
      }
      fprintf(stderr, "history: -d: option requires a numeric argument\n");
      return 1;
    }

    if (arg.str[0] == '-' && arg.len > 1) {
      flags = parse_flags(string__substring(arg, 1));
      if (flags.delete_entry) {
        waiting_for_offset = true;
        continue;
      }
    } else if (flags.print_expand) {
      do_not_save_history = true;
      return handle_print_expand(cmd, i) ? 0 : 1;
    } else if (flags.store) {
      return store_history_entry(cmd, i) ? 0 : 1;
    } else if (string__isdigit(arg)) {
      int n;
      if (!ratoi(arg, &n).is_err) {
        n = (n > hist_len) ? hist_len : n;
        for (int j = hist_len - n; j < hist_len; j++)
          print_history_entry(j, time_format, hist_list[j]);
        return 0;
      }
    }
  }

  if (waiting_for_offset) {
    fprintf(stderr, "history: -d: option requires a numeric argument\n");
    return 1;
  }

  if (flags.clear) {
    clear_history();
    return 0;
  }

  if (flags.read_file) {
    clear_history();
    return handle_file_operation("read_history", read_history) ? 0 : 1;
  }

  if (flags.read_new) return handle_file_operation("read_history", read_history) ? 0 : 1;
  if (flags.append) return handle_file_operation("append_history", append_history_wrapper) ? 0 : 1;
  if (flags.write) return handle_file_operation("write_history", write_history) ? 0 : 1;

  for (int i = 0; i < hist_len; i++)
    print_history_entry(i, time_format, hist_list[i]);
  return 0;
}