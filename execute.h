#ifndef __RICKSHELL_EXECUTE_H__
#define __RICKSHELL_EXECUTE_H__

#include <sys/types.h>
#include <sys/resource.h>
#include <stdbool.h>

#define MAX_REDIRECTS 10
#define MAX_INDENT 64

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
  bool is_fd;
} Redirect;

typedef struct Command {
  int argc;
  char** argv;
  Redirect redirects[MAX_REDIRECTS];
  int num_redirects;
  bool background;
  struct Command* next;
  struct CommandList* subcommand;
  bool and_next;
  bool or_next;
  uid_t uid;
  gid_t gid;
  char* working_directory;
  char** environment;
  int num_env_vars;
  mode_t umask;
  bool resource_limits_set;
  struct rlimit* resource_limits;
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
void print_command_list(const CommandList* list, int depth);
void set_pipeline_background(CommandList* list);

bool set_command_user(Command* cmd, uid_t uid, gid_t gid);
bool set_command_working_directory(Command* cmd, const char* directory);
bool add_environment_variable(Command* cmd, const char* name, const char* value);
void set_command_umask(Command* cmd, mode_t umask);
bool set_resource_limit(Command* cmd, int resource, const struct rlimit* rlim);

int parse_and_execute(const char* input);
#endif /* __RICKSHELL_EXECUTE_H__ */