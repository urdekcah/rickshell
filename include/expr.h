#ifndef __RICKSHELL_EXPR_H__
#define __RICKSHELL_EXPR_H__
#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>
#include "rick.h"
#include "rstring.h"
#include "array.h"

#define MAX_REDIRECTS 10

typedef struct Command {
  StringArray argv;
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
bool add_argument(Command* cmd, const string arg);
bool add_redirect(Command* cmd, RedirectType type, int fd, const string target);
bool add_pipeline(Command* __restrict cmd, Command* __restrict next);
CommandList* create_command_list(void);
bool add_command(CommandList* list, Command* cmd);
bool append_command_list(CommandList* __restrict dest, CommandList* __restrict src);
void free_command(Command* cmd);
void free_command_list(CommandList* list);
void set_pipeline_background(CommandList* list);
void set_command_background(Command* cmd);
#endif /* __RICKSHELL_EXPR_H__ */