#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include "job.h"
#include "expr.h"
#include "pipeline.h"
#include "memory.h"
#include "error.h"
#include "execute.h"

static JobList job_list = {NULL, NULL, 1};

void init_job_list(void) {
  job_list.first_job = NULL;
  job_list.last_job = NULL;
  job_list.next_job_id = 1;
}

Job* add_job(pid_t pgid, CommandList* cmds, const char* command_line) {
  Job* job = rmalloc(sizeof(Job));
  if (!job) {
    print_error("Failed to allocate memory for job");
    return NULL;
  }

  job->job_id = job_list.next_job_id++;
  job->pgid = pgid;
  job->cmds = cmds;
  job->command_line = strdup(command_line);
  job->status = 0;
  job->next = NULL;

  if (!job_list.first_job) {
    job_list.first_job = job;
    job_list.last_job = job;
  } else {
    job_list.last_job->next = job;
    job_list.last_job = job;
  }

  return job;
}

void remove_job(Job* job) {
  if (!job) return;

  Job* current = job_list.first_job;
  Job* prev = NULL;

  while (current) {
    if (current == job) {
      if (prev) {
        prev->next = current->next;
      } else {
        job_list.first_job = current->next;
      }

      if (job_list.last_job == job) {
        job_list.last_job = prev;
      }

      free(job->command_line);
      free(job);

      if (!job_list.first_job) {
        job_list.next_job_id = 1;
      }

      return;
    }

    prev = current;
    current = current->next;
  }
}

Job* find_job(int job_id) {
  Job* job = job_list.first_job;
  while (job) {
    if (job->job_id == job_id) {
      return job;
    }
    job = job->next;
  }
  return NULL;
}

void update_job_status(void) {
  Job* job = job_list.first_job;
  while (job) {
    int status;
    pid_t result = waitpid(-job->pgid, &status, WNOHANG | WUNTRACED);
    
    if (result > 0) {
      if (WIFEXITED(status) || WIFSIGNALED(status)) {
        job->status = status;
        Job* to_remove = job;
        job = job->next;
        remove_job(to_remove);
      } else {
        job->status = status;
        job = job->next;
      }
    } else {
      job = job->next;
    }
  }
}

int execute_background_job(CommandList* cmds, const char* command_line) {
  pid_t pid = fork();

  if (pid == 0) {
    setpgid(0, 0);
    int status = execute_command_list(cmds);
    exit(status);
  } else if (pid > 0) {
    setpgid(pid, pid);
    Job* job = add_job(pid, cmds, command_line);
    if (job) {
      printf("[%d] %d\n", job->job_id, pid);
    }
    return 0;
  } else {
    print_error("Failed to fork for background job");
    return -1;
  }
}

void print_jobs(void) {
  Job* job = job_list.first_job;
  while (job) {
    printf("[%d] Running\t%s\n", job->job_id, job->command_line);
    job = job->next;
  }
}

void cleanup_jobs(void) {
  Job* job = job_list.first_job;
  while (job) {
    Job* next = job->next;
    kill(-job->pgid, SIGKILL);
    remove_job(job);
    job = next;
  }
}

void print_job_status(void) {
  Job* job = job_list.first_job;
  Job* prev = NULL;
  while (job) {
    int status;
    pid_t result = waitpid(-job->pgid, &status, WNOHANG | WUNTRACED);
    
    if (result > 0) {
      if (WIFEXITED(status)) {
        printf("[%d]+ Done\t\t%s\n", job->job_id, job->command_line);
        Job* to_remove = job;
        job = job->next;
        remove_job(to_remove);
      } else if (WIFSIGNALED(status)) {
        printf("[%d]+ Terminated\t%s\n", job->job_id, job->command_line);
        Job* to_remove = job;
        job = job->next;
        remove_job(to_remove);
      } else {
        prev = job;
        job = job->next;
      }
    } else {
      prev = job;
      job = job->next;
    }
  }
  (void)prev;
}