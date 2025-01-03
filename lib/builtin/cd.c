#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "builtin.h"
#include "expr.h"
#include "error.h"
#include "rstring.h"
#include "array.h"
#include "io.h"

static char old_pwd[4096] = {0};

static int change_dir(const char *dir, int follow_symlinks, int exit_on_error, int print_new_dir) {
  char resolved_path[4096];
  char *real_path = NULL;
  
  if (!follow_symlinks) {
    if (chdir(dir) != 0) {
      if (exit_on_error) {
        ffprintln(stderr, "cd: error changing directory: %s", strerror(errno));
        exit(1);
      }
      return -1;
    }
  } else {
    real_path = realpath(dir, resolved_path);
    if (real_path == NULL) {
      if (exit_on_error) {
        ffprintln(stderr, "cd: error resolving path: %s", strerror(errno));
        exit(1);
      }
      return -1;
    }
    if (chdir(real_path) != 0) {
      if (exit_on_error) {
        ffprintln(stderr, "cd: error changing directory: %s", strerror(errno));
        exit(1);
      }
      return -1;
    }
  }
  
  char *new_pwd = getcwd(NULL, 0);
  if (new_pwd == NULL) {
    if (exit_on_error) {
      ffprintln(stderr, "cd: error getting current directory: %s", strerror(errno));
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
    fprintln("%s", new_pwd);
  }
  
  free(new_pwd);
  return 0;
}

static char *find_cd_path(const char *dir) {
  static char full_path[4096]; // This buffer size is still fine as long as it's large enough for most scenarios.
  char *cdpath = getenv("CDPATH");
  
  if (cdpath == NULL) {
    return NULL;
  }
  
  char *path;
  char *cdpath_copy = strdup(cdpath);
  char *saveptr;
  
  path = strtok_r(cdpath_copy, ":", &saveptr);
  while (path != NULL) {
    size_t required_length = strlen(path) + strlen(dir) + 2;
    if (required_length < sizeof(full_path)) {
      snprintf(full_path, sizeof(full_path), "%s/%s", path, dir);
      if (access(full_path, F_OK) == 0) {
        free(cdpath_copy);
        return full_path;
      }
    }
    
    path = strtok_r(NULL, ":", &saveptr);
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
  string dir = _SLIT0;

  for (size_t i = 1; i < cmd->argv.size; i++) {
    string elem = *(string*)array_get(cmd->argv, i);
    if (string__compare(elem, _SLIT("-L")) == 0) {
      follow_symlinks = 1;
      physical = 0;
    } else if (string__compare(elem, _SLIT("-P")) == 0) {
      follow_symlinks = 0;
      physical = 1;
    } else if (string__compare(elem, _SLIT("-e")) == 0) {
      exit_on_error = 1;
    } else if (string__compare(elem, _SLIT("-@")) == 0) {
      extended_attributes = 1;
    } else if (string__compare(elem, _SLIT("-")) == 0) {
      if (old_pwd[0] == '\0') {
        ffprintln(stderr, "cd: OLDPWD not set");
        return 1;
      }
      dir = string__new(old_pwd);
      print_new_dir = 1;
    } else if (elem.str[0] != '-') {
      dir = elem;
      break;
    } else {
      ffprintln(stderr, "cd: invalid option: %s", elem.str);
      ffprintln(stderr, "Usage: cd [-L|-P] [-e] [-@] [dir]");
      return 1;
    }
  }
  (void)physical;

  if (string__is_null_or_empty(dir)) {
    dir = string__new(getenv("HOME"));
    if (string__is_null_or_empty(dir)) {
      ffprintln(stderr, "cd: HOME not set");
      return 1;
    }
  }

  if (dir.str[0] != '/' && string__indexof(dir, _SLIT("/")) == -1) {
    char *cdpath_result = find_cd_path(dir.str);
    if (cdpath_result != NULL) {
      dir = string__new(cdpath_result);
      print_new_dir = 1;
    } else {
      const char* env_var_value = getenv(dir.str);
      if (env_var_value != NULL) {
        dir = string__new(env_var_value);
      }
    }
  }

  if (extended_attributes) {
    struct stat st;
    if (lstat(dir.str, &st) == 0 && S_ISREG(st.st_mode)) {
      ffprintln(stderr, "cd: %S: Not a directory (but has extended attributes)", dir);
      return 1;
    }
  }

  if (change_dir(dir.str, follow_symlinks, exit_on_error, print_new_dir) != 0) {
    ffprintln(stderr, "cd: %S: %s", dir, strerror(errno));
    return 1;
  }

  return 0;
}