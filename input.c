#define _POSIX_C_SOURCE 1
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <linux/limits.h>
#include <termios.h>
#include <ctype.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "color.h"
#include "error.h"
#include "result.h"
#include "rstring.h"
#include "builtin.h"
#include "file.h"
#include "io.h"

#define INITIAL_BUFFER_SIZE 256
#define CTRL_KEY(k) ((k) & 0x1f)

extern char *path_dirs[MAX_PATH_DIRS];
extern int path_dir_count;
static InputBuffer input = {NULL, 0, 0, 0};

static void init_input_buffer() {
  input.buf = malloc(INITIAL_BUFFER_SIZE);
  input.capacity = INITIAL_BUFFER_SIZE;
  input.len = 0;
  input.cursor_pos = 0;
}

static int is_command_char(char c) {
  return isalnum(c) || c == '_' || c == '-' || c == '.';
}

static void get_command_length(const char* buf, size_t* cmd_len) {
  *cmd_len = 0;
  while (buf[*cmd_len] && is_command_char(buf[*cmd_len])) {
    (*cmd_len)++;
  }
}

static int command_exists(const string cmd) {
  if (get_builtin_func(cmd) != NULL) {
    return 1;
  }

  StringBuilder s = string_builder__new();
  struct stat st;

  for (int i = 0; i < path_dir_count; i++) {
    string_builder__append_cstr(&s, path_dirs[i]);
    string_builder__append_char(&s, '/');
    string_builder__append(&s, cmd);
    string full_path = string_builder__to_string(&s);
    string_builder__free(&s);

    if (stat(full_path.str, &st) == 0 && (st.st_mode & S_IXUSR)) {
      string__free(full_path);
      string_builder__free(&s);
      return 1;
    }
    string__free(full_path);
    string_builder__free(&s);
    s = string_builder__new();
  }
  string_builder__free(&s);

  return 0;
}

static void refresh_line() {
  printf("\r\033[K");
  print_prompt();

  if (input.len > 0) {
    size_t cmd_len = 0;
    get_command_length(input.buf, &cmd_len);
        
    if (cmd_len > 0) {
      string temp = string__new(input.buf);
      string cmd = string__substring(temp, 0, (ssize_t)cmd_len);
      bool cmd_exists = command_exists(cmd);
      string__free(temp);
      string__free(cmd);

      printf("%s%.*s%s", (cmd_exists)?ANSI_COLOR_GREEN:ANSI_COLOR_RED, (int)cmd_len, input.buf, ANSI_COLOR_RESET);
      if (cmd_len < input.len)
        printf("%s", input.buf + cmd_len);
    } else {
      printf("%s", input.buf);
    }
  }
  
  if (input.cursor_pos < input.len) {
    printf("\033[%zuD", input.len - input.cursor_pos);
  }
    
  fflush(stdout);
}

static void insert_char(char c) {
  if (input.len + 1 >= input.capacity) {
    size_t new_capacity = input.capacity * 2;
    char *new_buf = realloc(input.buf, new_capacity);
    if (new_buf) {
      input.buf = new_buf;
      input.capacity = new_capacity;
    }
  }
    
  if (input.cursor_pos < input.len) {
    memmove(input.buf + input.cursor_pos + 1, 
            input.buf + input.cursor_pos, 
            input.len - input.cursor_pos);
  }
    
  input.buf[input.cursor_pos] = c;
  input.cursor_pos++;
  input.len++;
  input.buf[input.len] = '\0';
}

static void delete_char() {
  if (input.cursor_pos > 0) {
    if (input.cursor_pos < input.len) {
      memmove(input.buf + input.cursor_pos - 1, 
              input.buf + input.cursor_pos, 
              input.len - input.cursor_pos);
    }
    input.cursor_pos--;
    input.len--;
    input.buf[input.len] = '\0';
  }
}

StringResult get_input(string* result) {
  if (!input.buf) {
    init_input_buffer();
  }
  
  enable_raw_mode();
  input.len = 0;
  input.cursor_pos = 0;
  input.buf[0] = '\0';
  
  while (1) {
    refresh_line("$ ");
    char c;
    ssize_t nread = read(STDIN_FILENO, &c, 1);
    
    if (nread == -1) {
      if (errno == EAGAIN) continue;
      disable_raw_mode();
      return Err(_SLIT("read error"), ERRCODE_INPUT_READ_FAILED);
    }
    
    if (nread == 0) {
      if (input.len == 0) {
        disable_raw_mode();
        return Err(_SLIT("EOF"), ERRCODE_EOF);
      }
      break;
    }

    switch (c) {
      case CTRL_KEY('c'):
        printf("\n");
        input.len = 0;
        input.buf[0] = '\0';
        disable_raw_mode();
        return Ok(NULL);
        
      case '\r':
      case '\n':
        printf("\n");
        if (input.len > 0) {
          *result = string__new(input.buf);
          disable_raw_mode();
          return Ok(NULL);
        }
        break;
        
      case 127:
      case CTRL_KEY('h'):
        delete_char();
        break;
        
      case CTRL_KEY('d'):
        if (input.len == 0) {
          disable_raw_mode();
          return Err(_SLIT("EOF"), ERRCODE_EOF);
        }
        break;
        
      default:
        if (!iscntrl(c)) {
          insert_char(c);
        }
        break;
    }
  }
  
  disable_raw_mode();
  return Ok(NULL);
}