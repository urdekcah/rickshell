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
#define MAX_ARG_LENGTH 4096

extern CommandList* command_list;

Command* create_command(void) {
  Command* cmd = rmalloc(sizeof(Command));
  if (cmd == NULL) {
    print_error(_SLIT("Command creation failed"));
    exit(EXIT_FAILURE);
  }
  memset(cmd, 0, sizeof(Command));
  cmd->redirects.data = rcalloc(1, sizeof(Redirect));
  cmd->redirects.capacity = 1;
  cmd->argv = create_array(sizeof(string));
  return cmd;
}

bool add_argument(Command* cmd, const string arg) {
  if (cmd == NULL || string__is_null_or_empty(arg)) return false;
  
  if (cmd->argv.size >= INT_MAX - 1) {
    print_error(_SLIT("Maximum number of arguments exceeded"));
    return false;
  }

  array_push(&cmd->argv, (string*)&arg);
  return true;
}

bool add_redirect(Command* cmd, RedirectType type, int fd, const string target) {
  if (cmd == NULL || string__is_null_or_empty(target) || cmd->redirects.size >= MAX_REDIRECTS) return false;
  
  if (cmd->redirects.size >= cmd->redirects.capacity) {
    size_t new_capacity = cmd->redirects.capacity * 2;
    cmd->redirects.data = rrealloc(cmd->redirects.data, new_capacity * sizeof(Redirect));
    cmd->redirects.capacity = new_capacity;
  }
  
  Redirect* redirect = &cmd->redirects.data[cmd->redirects.size];
  redirect->type = type;
  redirect->fd = fd;
  redirect->target = string__from(target);
  redirect->is_fd = (target.str[0] >= '0' && target.str[0] <= '9' && target.str[1] == '\0');
  
  cmd->redirects.size++;
  return true;
}

bool add_pipeline(Command* __restrict cmd, Command* __restrict next) {
  if (cmd == NULL || next == NULL) return false;
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
    print_error(_SLIT("Command list creation failed"));
    exit(EXIT_FAILURE);
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

bool append_command_list(CommandList* __restrict dest, CommandList* __restrict src) {
  if (dest == NULL || src == NULL) return false;
  
  if (dest->head == NULL) {
    dest->head = src->head;
    dest->tail = src->tail;
  } else if (src->head != NULL) {
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
    string__free(*(string*)array_checked_get(cmd->argv, i));
  array_free(&cmd->argv);
  for (size_t i = 0; i < cmd->redirects.size; i++)
    string__free(cmd->redirects.data[i].target);
  rfree(cmd->redirects.data);
  memset(cmd, 0, sizeof(Command));
  rfree(cmd);
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
  rfree(list);
}

void set_pipeline_background(CommandList* list) {
  if (list == NULL) return;
  for (Command* cmd = list->head; cmd != NULL; cmd = cmd->next) {
    cmd->background = true;
  }
}

void set_command_background(Command* cmd) {
  if (cmd != NULL) {
    cmd->background = true;
  }
}