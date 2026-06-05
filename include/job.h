#ifndef __RICKSHELL_JOB_H__
#define __RICKSHELL_JOB_H__
#include <sys/types.h>
#include "expr.h"
#include "result.h"

typedef struct Job {
  int job_id;
  pid_t pgid;
  CommandList* cmds;
  string command_line;
  int status;
  struct Job* next;
} Job;

typedef struct {
  Job* first_job;
  Job* last_job;
  int next_job_id;
} JobList;

/* Interactive shell job-control setup. Must be called once at start-up,
 * before any signal dispositions are changed (it relies on the default
 * SIGTTIN action while it moves the shell into the foreground). */
void init_shell(void);
/* True when stdin is a terminal and job control is available. */
int shell_is_interactive(void);
/* True only in the interactive shell process while it is the foreground job
 * driver (i.e. not inside a forked background child). Decides whether a child
 * should be placed in its own process group and handed the terminal. */
int job_control_active(void);
/* Hand the controlling terminal to process group PGID / take it back. Safe to
 * call from both the parent and the just-forked child (no-op when the shell is
 * not interactive). */
void give_terminal_to(pid_t pgid);
void reclaim_terminal(void);
/* Restore default dispositions for the signals the shell catches/ignores, to
 * be called in a forked child after fork() and before exec(). */
void reset_child_signals(void);

void init_job_list(void);
Job* add_job(pid_t pgid, CommandList* cmds, const string command_line);
void remove_job(Job* job);
Job* find_job(int job_id);
void update_job_status(void);
/**
 * @param[in]  cmds
 * @param[in]  command_line
 * @param[out] result
 */
IntResult execute_background_job(CommandList* cmds, const string command_line, int* result);
void print_jobs(void);
void cleanup_jobs(void);
void print_job_status(void);
void check_background_jobs(void);
#endif /* __RICKSHELL_JOB_H__ */