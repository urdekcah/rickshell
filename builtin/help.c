#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "builtin.h"
#include "rstring.h"
#include "io.h"

extern const HelpInfo builtin_helps[BUILTIN_FUNCS_SIZE];

static void print_short_usage(const HelpInfo* info) {
  fprintln("%S: %S", info->name, info->usage);
}

static void print_short_desc(const HelpInfo* info) {
  fprintln("%S - %S", info->name, info->short_desc);
}

static void print_long_desc(const HelpInfo* info) {
  fprint("%S: %S\n\n%S", info->name, info->usage, info->long_desc);
}

static void print_manpage_format(const HelpInfo* info) {
  fprint("NAME\n  %S - %S\n\n", info->name, info->short_desc);
  fprint("SYNOPSIS\n  %S\n\n", info->usage);
  fprint("DESCRIPTION\n%S", info->long_desc);
}

int builtin_help(Command *cmd) {
  bool opt_d = false, opt_m = false, opt_s = false;
  size_t i;

  for (i = 1; i < cmd->argv.size; i++) {
    string arg = *(string*)array_get(cmd->argv, i);
    if (string__equals(arg, _SLIT("-d"))) opt_d = true;
    else if (string__equals(arg, _SLIT("-m"))) opt_m = true;
    else if (string__equals(arg, _SLIT("-s"))) opt_s = true;
    else break;
  }

  if (i == cmd->argv.size) {
    for (size_t j = 0; j < sizeof(builtin_helps) / sizeof(HelpInfo); j+=2) {
      string first, second;
      first = builtin_helps[j].usage;
      second = (j+1 < sizeof(builtin_helps) / sizeof(HelpInfo)) ? builtin_helps[j+1].usage : _SLIT0;
      printf("%-60s %s\n", first.str, second.str);
    }
    return 0;
  }

  bool found = false;
  for (; i < cmd->argv.size; i++) {
    string pattern = *(string*)array_get(cmd->argv, i);
    for (size_t j = 0; j < sizeof(builtin_helps) / sizeof(HelpInfo); j++) {
      if (strstr(builtin_helps[j].name.str, pattern.str) != NULL) {
        found = true;
        if (opt_s) print_short_usage(&builtin_helps[j]);
        else if (opt_d) print_short_desc(&builtin_helps[j]);
        else if (opt_m) print_manpage_format(&builtin_helps[j]);
        else print_long_desc(&builtin_helps[j]);
      }
    }
  }

  if (!found) {
    fprintf(stderr, "help: no help topics match '%s'\n", ((string*)array_get(cmd->argv, i-1))->str);
    return 1;
  }

  return 0;
}