#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include "expr.h"
#include "redirect.h"
#include "memory.h"
#include "error.h"

#define INITIAL_ARGV_SIZE 10

extern CommandList* command_list;

static char* safe_strdup(const char* str) {
  if (str == NULL) return NULL;
  char* new_str = strdup(str);
  if (new_str == NULL) {
    print_error("Memory allocation failed");
    exit(EXIT_FAILURE);
  }
  return new_str;
}

Command* create_command(void) {
  Command* cmd = (Command*)rmalloc(sizeof(Command));
  memset(cmd, 0, sizeof(Command));
  cmd->redirects.data = (Redirect*)rcalloc(1, sizeof(Redirect));
  cmd->redirects.capacity = 1;
  cmd->redirects.size = 0;
  cmd->argv.data = (char**)rcalloc(1, sizeof(char*));
  cmd->argv.capacity = 1;
  cmd->argv.size = 0;
  return cmd;
}

bool add_argument(Command* cmd, const char* arg) {
  if (cmd == NULL || arg == NULL) return false;
    
  if (cmd->argv.size >= INT_MAX - 1) {
    print_error("Maximum number of arguments exceeded");
    return false;
  }
    
  if (cmd->argv.size >= cmd->argv.capacity - 1) {
    size_t new_capacity = cmd->argv.capacity * 2;
    size_t new_size;
    if (__builtin_mul_overflow(new_capacity, sizeof(char*), &new_size)) {
      print_error("Integer overflow in argument allocation");
      return false;
    }
    char** new_argv = rrealloc(cmd->argv.data, new_size);
    if (new_argv == NULL) return false;
    cmd->argv.data = new_argv;
    cmd->argv.capacity = new_capacity;
  }
    
  cmd->argv.data[cmd->argv.size] = safe_strdup(arg);
  if (cmd->argv.data[cmd->argv.size] == NULL) {
    return false;
  }
  cmd->argv.size++;
  cmd->argv.data[cmd->argv.size] = NULL;
  return true;
}

bool add_redirect(Command* cmd, RedirectType type, int fd, const char* target) {
  if (cmd == NULL || target == NULL || cmd->redirects.size >= MAX_REDIRECTS) return false;
  
  Redirect* redirect = &cmd->redirects.data[cmd->redirects.size];
  redirect->type = type;
  redirect->fd = fd;
  redirect->target = safe_strdup(target);
  redirect->is_fd = (target[0] >= '0' && target[0] <= '9' && target[1] == '\0');
  
  cmd->redirects.size++;
  return true;
}

bool add_pipeline(Command* cmd, Command* next) {
  if (cmd == NULL) return false;
  Command* last = cmd;
  while (last->next != NULL) {
    last = last->next;
  }
  last->next = next;
  last->pipline_next = true;
  return true;
}

CommandList* create_command_list(void) {
  CommandList* list = rcalloc(1, sizeof(CommandList));
  if (list == NULL) {
    print_error("Memory allocation failed");
    return NULL;
  }
  return list;
}

bool add_command(CommandList* list, Command* cmd) {
  if (list == NULL || cmd == NULL) return false;
  
  if (list->head == NULL) {
    list->head = cmd;
    list->tail = cmd;
  } else {
    list->tail->next = cmd;
    list->tail = cmd;
  }
  return true;
}

bool append_command_list(CommandList* dest, CommandList* src) {
  if (dest == NULL || src == NULL) return false;
  
  if (dest->head == NULL) {
    dest->head = src->head;
    dest->tail = src->tail;
  } else {
    dest->tail->next = src->head;
    dest->tail = src->tail;
  }
  
  src->head = NULL;
  src->tail = NULL;
  return true;
}

void free_command(Command* cmd) {
  if (cmd == NULL) return;
  for (size_t i = 0; i < cmd->argv.size; i++)
    free(cmd->argv.data[i]);
  free(cmd->argv.data);
  for (size_t i = 0; i < cmd->redirects.size; i++)
    free(cmd->redirects.data[i].target);
  free(cmd->redirects.data);
  memset(cmd, 0, sizeof(Command));
  free(cmd);
}

void free_command_list(CommandList* list) {
  if (list == NULL) return;
  
  Command* current = list->head;
  while (current != NULL) {
    Command* next = current->next;
    free_command(current);
    current = next;
  }
  
  memset(list, 0, sizeof(CommandList));
  free(list);
}

void set_pipeline_background(CommandList* list) {
  if (list == NULL) return;
  Command* cmd = list->head;
  while (cmd != NULL) {
    cmd->background = true;
    cmd = cmd->next;
  }
}