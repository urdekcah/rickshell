#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__

#include <sys/types.h>
#include <sys/resource.h>

#define MAX_REDIRECTS 10

typedef enum {
  REDIRECT_INPUT,
  REDIRECT_OUTPUT,
  REDIRECT_APPEND,
  REDIRECT_INPUT_DUP,
  REDIRECT_OUTPUT_DUP,
  REDIRECT_APPEND_DUP
} RedirectType;

typedef struct Redirect {
  RedirectType type;
  int fd;
  char* target;
  int is_fd;
} Redirect;

typedef struct Command {
  int argc;
  char** argv;
  Redirect redirects[MAX_REDIRECTS];
  int num_redirects;
  int background;
  struct Command* next;
  struct CommandList* subcommand;
  int and_next;
  int or_next;
  uid_t uid;
  gid_t gid;
  char* working_directory;
  char** environment;
  int num_env_vars;
  int umask;
  int resource_limits_set;
  struct rlimit* resource_limits;
} Command;

typedef struct CommandList {
  Command* head;
  Command* tail;
} CommandList;

Command* create_command();
void add_argument(Command* cmd, char* arg);
void add_redirect(Command* cmd, RedirectType type, int fd, char* target);
void add_pipeline(Command* cmd, Command* next);
CommandList* create_command_list();
void add_command(CommandList* list, Command* cmd);
void append_command_list(CommandList* dest, CommandList* src);
void free_command(Command* cmd);
void free_command_list(CommandList* list);
void print_command_list(CommandList* list, int depth);
void set_pipeline_background(CommandList* list);

void set_command_user(Command* cmd, uid_t uid, gid_t gid);
void set_command_working_directory(Command* cmd, const char* directory);
void add_environment_variable(Command* cmd, const char* name, const char* value);
void set_command_umask(Command* cmd, int umask);
void set_resource_limit(Command* cmd, int resource, const struct rlimit* rlim);

int parse_and_execute(char* input);
#endif /* __RICKSHELL_EXECUTE_H__ */