#define _XOPEN_SOURCE 700
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <termios.h>
#include <errno.h>
#include "job.h"
#include "expr.h"
#include "pipeline.h"
#include "memory.h"
#include "error.h"
#include "execute.h"
#include "io.h"

static JobList job_list = {NULL, NULL, 1};

/* ------------------------------------------------------------------ *
 *  Shell-level job control (foreground terminal ownership)            *
 * ------------------------------------------------------------------ */
static pid_t shell_pgid = -1;
static int shell_terminal = -1;
static int s_is_interactive = 0;
static struct termios shell_tmodes;
static int s_have_tmodes = 0;

void init_shell(void) {
  shell_terminal = STDIN_FILENO;
  s_is_interactive = isatty(shell_terminal) ? 1 : 0;

  if (!s_is_interactive) {
    /* Record our pgid anyway so job_control_active() stays well defined. */
    shell_pgid = getpid();
    return;
  }

  /* If we are not in the foreground (e.g. the shell itself was launched in the
   * background), stop ourselves with SIGTTIN until a parent moves us forward.
   * This must run while SIGTTIN still has its default action. */
  int guard = 0;
  pid_t fg;
  while ((fg = tcgetpgrp(shell_terminal)) >= 0 && fg != (shell_pgid = getpgrp())) {
    if (kill(-shell_pgid, SIGTTIN) < 0) break;
    if (++guard > 1000) break;
  }

  /* Put ourselves in our own process group. A session leader cannot change its
   * pgid (EPERM); that is fine, it already leads its own group. */
  shell_pgid = getpid();
  if (setpgid(shell_pgid, shell_pgid) < 0) {
    if (errno != EPERM)
      perror("rickshell: setpgid");
  }

  /* Take control of the terminal and remember its attributes. */
  if (tcsetpgrp(shell_terminal, shell_pgid) < 0) {
    /* No terminal control available: fall back to non-interactive job control. */
    s_is_interactive = 0;
    return;
  }
  if (tcgetattr(shell_terminal, &shell_tmodes) == 0)
    s_have_tmodes = 1;
}

int shell_is_interactive(void) {
  return s_is_interactive;
}

int job_control_active(void) {
  return s_is_interactive && getpid() == shell_pgid;
}

void give_terminal_to(pid_t pgid) {
  if (s_is_interactive && shell_terminal >= 0)
    tcsetpgrp(shell_terminal, pgid);
}

void reclaim_terminal(void) {
  if (!s_is_interactive || shell_terminal < 0) return;
  tcsetpgrp(shell_terminal, shell_pgid);
  if (s_have_tmodes)
    tcsetattr(shell_terminal, TCSADRAIN, &shell_tmodes);
}

void reset_child_signals(void) {
  struct sigaction sa = {0};
  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  /* The shell catches SIGINT and ignores SIGQUIT; restore both so the exec'd
   * program gets normal terminal-signal behaviour. SIGTSTP/SIGTTIN/SIGTTOU are
   * deliberately left ignored: this shell has no fg/bg to resume a stopped job,
   * so making foreground children stoppable would only strand them. */
  sigaction(SIGINT, &sa, NULL);
  sigaction(SIGQUIT, &sa, NULL);
}

void init_job_list(void) {
  job_list.first_job = NULL;
  job_list.last_job = NULL;
  job_list.next_job_id = 1;
}

Job* add_job(pid_t pgid, Node* cmds, const string command_line) {
  Job* job = rmalloc(sizeof(Job));
  if (!job) {
    print_error(_SLIT("Failed to allocate memory for job"));
    return NULL;
  }

  job->job_id = job_list.next_job_id++;
  job->pgid = pgid;
  job->cmds = cmds;
  job->command_line = string__from(command_line);
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

      string__free(job->command_line);
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

IntResult execute_background_job(Node* node, const string command_line, int* result) {
  fflush(NULL);
  pid_t pid = fork();

  *result = 0;

  if (pid == 0) {
    setpgid(0, 0);
    *result = 0;
    execute_node(node, result);
    exit(*result);
  } else if (pid > 0) {
    setpgid(pid, pid);
    Job* job = add_job(pid, node, command_line);
    if (job) {
      fprintln("[%d] %d", job->job_id, pid);
    }
    return Ok(NULL);
  } else {
    return Err(
      _SLIT("Failed to fork for background job"),
      ERRCODE_EXEC_FORK_FAILED
    );
  }
}

void print_jobs(void) {
  Job* job = job_list.first_job;
  while (job) {
    fprintln("[%d] Running\t%S", job->job_id, job->command_line);
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
        fprintln("[%d]+ Done\t\t%S", job->job_id, job->command_line);
        Job* to_remove = job;
        job = job->next;
        remove_job(to_remove);
      } else if (WIFSIGNALED(status)) {
        fprintln("[%d]+ Terminated\t%S", job->job_id, job->command_line);
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