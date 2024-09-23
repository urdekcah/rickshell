#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include "memory.h"

char* expand_home_directory(const char* path) {
  if (path[0] == '~') {
    const char* home = getenv("HOME");
    if (home == NULL) {
      fprintf(stderr, "Could not get HOME environment variable.\n");
      return NULL;
    }

    size_t len = strlen(home) + strlen(path); 
    char* expanded_path = (char*)rmalloc(len);

    snprintf(expanded_path, len, "%s%s", home, path + 1); 
    return expanded_path;
  }
  return rstrdup(path);
}

int ensure_directory_exist(const char *dir_path) {
  struct stat st;
  char* expanded_path = expand_home_directory(dir_path);

  if (stat(expanded_path, &st) == 0 && S_ISDIR(st.st_mode)) {
    rfree(expanded_path);
    return 0;
  }

  char *parent_path = rstrdup(expanded_path);
  char *last_slash = strrchr(parent_path, '/');
  
  if (last_slash != NULL && last_slash != parent_path) {
    *last_slash = '\0';
    ensure_directory_exist(parent_path);
  }

  if (mkdir(expanded_path, 0755) != 0) {
    if (errno != EEXIST) {
      perror("mkdir failed");
      rfree(parent_path);
      rfree(expanded_path);
      return -1;
    }
  }

  rfree(parent_path);
  rfree(expanded_path);
  return 0;
}

int ensure_file_exist(const char *file_path) {
  char* expanded_path = expand_home_directory(file_path);
  int fd = open(expanded_path, O_CREAT | O_EXCL, 0644);
  if (fd == -1) {
    if (errno == EEXIST) {
      return 0;
    } else {
      perror("open failed");
      return -1;
    }
  }
  close(fd);
  rfree(expanded_path);
  return 0;
}