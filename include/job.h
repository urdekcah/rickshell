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