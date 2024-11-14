#ifndef __RICKSHELL_HISTORY_H__
#define __RICKSHELL_HISTORY_H__
#include <stdbool.h>
#define DEFAULT_HISTFILE "~/.rickshell/.history"

typedef struct {
  bool clear;
  bool delete_entry;
  bool append;
  bool read_new;
  bool read_file;
  bool write;
  bool print_expand;
  bool store;
  int delete_offset;
} HistoryFlags;
typedef int (*history_op_t)(const char*);

void save_history(char* cmd);
void reset_last_command(void);
#endif