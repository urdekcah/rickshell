#ifndef __RICKSHELL_EXPR_H__
#define __RICKSHELL_EXPR_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "rick.h"

#define MAX_REDIRECTS 10

typedef struct Command {
  struct {
    char** data;
    size_t capacity;
    size_t size;
  } argv;
  struct {
    Redirect* data;
    size_t capacity;
    size_t size;
  } redirects;
  bool background;
  struct Command* next;
  bool and_next;
  bool or_next;
  bool semi_next;
  bool pipline_next;
  bool is_subcommand;
} Command;

typedef struct CommandList {
  Command* head;
  Command* tail;
} CommandList;

Command* create_command(void);
bool add_argument(Command* cmd, const char* arg);
bool add_redirect(Command* cmd, RedirectType type, int fd, const char* target);
bool add_pipeline(Command* cmd, Command* next);
CommandList* create_command_list(void);
bool add_command(CommandList* list, Command* cmd);
bool append_command_list(CommandList* dest, CommandList* src);
void free_command(Command* cmd);
void free_command_list(CommandList* list);
void set_pipeline_background(CommandList* list);
#endif /* __RICKSHELL_EXPR_H__ */