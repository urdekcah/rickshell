#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include "builtin.h"
#include "expr.h"
#include "error.h"

static char old_pwd[4096] = {0};

static int change_dir(const char *dir, int follow_symlinks, int exit_on_error, int print_new_dir) {
  char resolved_path[4096];
  char *real_path = NULL;
  
  if (!follow_symlinks) {
    if (chdir(dir) != 0) {
      if (exit_on_error) {
        fprintf(stderr, "cd: error changing directory: %s\n", strerror(errno));
        exit(1);
      }
      return -1;
    }
  } else {
    real_path = realpath(dir, resolved_path);
    if (real_path == NULL) {
      if (exit_on_error) {
        fprintf(stderr, "cd: error resolving path: %s\n", strerror(errno));
        exit(1);
      }
      return -1;
    }
    if (chdir(real_path) != 0) {
      if (exit_on_error) {
        fprintf(stderr, "cd: error changing directory: %s\n", strerror(errno));
        exit(1);
      }
      return -1;
    }
  }
  
  char *new_pwd = getcwd(NULL, 0);
  if (new_pwd == NULL) {
    if (exit_on_error) {
      fprintf(stderr, "cd: error getting current directory: %s\n", strerror(errno));
      exit(1);
    }
    return -1;
  }
  
  char *current_pwd = getenv("PWD");
  if (current_pwd) {
    strncpy(old_pwd, current_pwd, 4096 - 1);
    old_pwd[4096 - 1] = '\0';
  }
  
  setenv("PWD", new_pwd, 1);
  
  if (print_new_dir) {
    printf("%s\n", new_pwd);
  }
  
  free(new_pwd);
  return 0;
}

static char *find_cd_path(const char *dir) {
  static char full_path[4096];
  char *cdpath = getenv("CDPATH");
  
  if (cdpath == NULL) {
    return NULL;
  }
  
  char *path;
  char *cdpath_copy = strdup(cdpath);
  char *saveptr;
  
  path = strtok_r(cdpath_copy, ":", &saveptr);
  while (path != NULL) {
    if (strlen(path) + strlen(dir) + 1 < sizeof(full_path)) {
      snprintf(full_path, sizeof(full_path), "%s/%s", path, dir);
      if (access(full_path, F_OK) == 0) {
        free(cdpath_copy);
        return full_path;
      }
      path = strtok_r(NULL, ":", &saveptr);
    }
  }
  
  free(cdpath_copy);
  return NULL;
}

int builtin_cd(Command *cmd) {
  int follow_symlinks = 1;
  int physical = 0;
  int exit_on_error = 0;
  int extended_attributes = 0;
  int print_new_dir = 0;
  const char *dir = NULL;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    if (strcmp(cmd->argv.data[i], "-L") == 0) {
      follow_symlinks = 1;
      physical = 0;
    } else if (strcmp(cmd->argv.data[i], "-P") == 0) {
      follow_symlinks = 0;
      physical = 1;
    } else if (strcmp(cmd->argv.data[i], "-e") == 0) {
      exit_on_error = 1;
    } else if (strcmp(cmd->argv.data[i], "-@") == 0) {
      extended_attributes = 1;
    } else if (strcmp(cmd->argv.data[i], "-") == 0) {
      if (old_pwd[0] == '\0') {
        fprintf(stderr, "cd: OLDPWD not set\n");
        return 1;
      }
      dir = old_pwd;
      print_new_dir = 1;
    } else if (cmd->argv.data[i][0] != '-') {
      dir = cmd->argv.data[i];
      break;
    } else {
      fprintf(stderr, "cd: invalid option: %s\n", cmd->argv.data[i]);
      fprintf(stderr, "Usage: cd [-L|-P] [-e] [-@] [dir]\n");
      return 1;
    }
  }
  (void)physical;

  if (dir == NULL) {
    dir = getenv("HOME");
    if (dir == NULL) {
      fprintf(stderr, "cd: HOME not set\n");
      return 1;
    }
  }

  if (dir[0] != '/' && strchr(dir, '/') == NULL) {
    char *cdpath_result = find_cd_path(dir);
    if (cdpath_result != NULL) {
      dir = cdpath_result;
      print_new_dir = 1;
    } else {
      char *var_value = getenv(dir);
      if (var_value != NULL) {
        dir = var_value;
      }
    }
  }

  if (extended_attributes) {
    struct stat st;
    if (lstat(dir, &st) == 0 && S_ISREG(st.st_mode)) {
      fprintf(stderr, "cd: %s: Not a directory (but has extended attributes)\n", dir);
      return 1;
    }
  }

  if (change_dir(dir, follow_symlinks, exit_on_error, print_new_dir) != 0) {
    fprintf(stderr, "cd: %s: %s\n", dir, strerror(errno));
    return 1;
  }

  return 0;
}