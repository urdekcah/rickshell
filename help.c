#include <stdio.h>
#include "rstring.h"
#include "builtin.h"

const HelpInfo builtin_helps[BUILTIN_FUNCS_SIZE] = {
  {
    _SLIT("cd"),
    _SLIT("Change the shell working directory"),
    _SLIT("cd [-L|-P] [-e] [-@] [dir]"),
    _SLIT(
      "Change the shell working directory.\n"
      "\n"
      "Options:\n"
      "  -L  Force symbolic links to be followed\n"
      "  -P  Use the physical directory structure without following symbolic links\n"
      "  -e  Exit if the directory does not exist\n"
      "  -@  Display extended attributes\n"
    )
  },
  {
    _SLIT("declare"),
    _SLIT("Declare variables and give them attributes"),
    _SLIT("declare: usage: declare [-aAilnprux] [-I] [name[=value] ...]"),
    _SLIT(
      "Declare variables and give them attributes.\n"
      "\n"
      "Options:\n"
      "  -a  Declare an array\n"
      "  -A  Declare an associative array\n"
      "  -i  Declare an integer\n"
      "  -u  Convert to uppercase\n"
      "  -l  Convert to lowercase\n"
      "  -n  Declare a nameref\n"
      "  -r  Declare a readonly variable\n"
      "  -x  Declare an exported variable\n"
      "  -p  Print all declared variables\n"
      "  -I  Inherit variables from the environment\n"
    )
  },
  {
    _SLIT("echo"),
    _SLIT("Display a line of text"),
    _SLIT("echo [-neE] [arg ...]"),
    _SLIT(
      "Display a line of text.\n"
      "\n"
      "Options:\n"
      "  -n  Do not output the trailing newline\n"
      "  -e  Interpret backslash escapes\n"
      "  -E  Do not interpret backslash escapes\n"
    )
  },
  {
    _SLIT("exit"),
    _SLIT("Exit the shell"),
    _SLIT("exit [n]"),
    _SLIT(
      "Exit the shell.\n"
      "\n"
      "Arguments:\n"
      "  n  Return an integer status n\n"
    )
  },
  {
    _SLIT("export"),
    _SLIT("Set an environment variable"),
    _SLIT("export [-np] [name[=value] ...]"),
    _SLIT(
      "Set an environment variable.\n"
      "\n"
      "Options:\n"
      "  -n  Unset the variable\n"
      "  -p  Print all exported variables\n"
    )
  },
  {
    _SLIT("help"),
    _SLIT("Display help information"),
    _SLIT("help [-dms] [pattern ...]"),
    _SLIT(
      "Display help information.\n"
      "\n"
      "Options:\n"
      "  -d  Display a short description\n"
      "  -m  Display a manpage format\n"
      "  -s  Display a short usage\n"
    )
  },
  {
    _SLIT("history"),
    _SLIT("Display the command history"),
    _SLIT("history: history [-c] [-d offset] [n] or history -anrw [filename] or history -ps arg [arg...]"),
    _SLIT(
      "Display the command history.\n"
      "\n"
      "Options:\n"
      "  -c  Clear the history list\n"
      "  -d  Delete the history entry at offset\n"
      "  -a  Append history lines from the history file\n"
      "  -n  Read all history lines not already read\n"
      "  -r  Read the history file and append the contents to the history list\n"
      "  -w  Write the current history to the history file\n"
      "  -p  Print the history list\n"
    )
  },
  {
    _SLIT("printf"),
    _SLIT("Format and print data"),
    _SLIT("printf [-v var] format [arguments]"),
    _SLIT(
      "Format and print data.\n"
      "\n"
      "Options:\n"
      "  -v  Store the output in the variable var\n"
    )
  },
  {
    _SLIT("readonly"),
    _SLIT("Mark variables as readonly"),
    _SLIT("readonly [-paA] [name ...]"),
    _SLIT(
      "Mark variables as readonly.\n"
      "\n"
      "Options:\n"
      "  -p  Print all readonly variables\n"
      "  -a  Refer to indexed arrays\n"
      "  -A  Refer to associative arrays\n"
    )
  },
  {
    _SLIT("set"),
    _SLIT("Set or unset shell options"),
    _SLIT("set [arg ...]"),
    _SLIT(
      ""
    )
  },
  {
    _SLIT("unset"),
    _SLIT("Unset variables and attributes"),
    _SLIT("unset [-vn] [name ...]"),
    _SLIT(
      "Unset variables and attributes.\n"
      "\n"
      "Options:\n"
      "  -v  Unset variables\n"
      "  -n  Unset namerefs\n"
    )
  }
};