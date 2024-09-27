#ifndef __RICKSHELL_VARIABLE_H__
#define __RICKSHELL_VARIABLE_H__
#include <stdbool.h>

typedef struct {
  char* name;
  char* value;
  bool readonly;
  bool is_array;
  char** array_values;
  int array_size;
} Variable;

typedef struct {
  Variable* variables;
  int size;
  int capacity;
} VariableTable;

VariableTable* create_variable_table();
void free_variable_table(VariableTable* table);
Variable* set_variable(VariableTable* table, const char* name, const char* value, bool readonly);
Variable* get_variable(VariableTable* table, const char* name);
void unset_variable(VariableTable* table, const char* name);
char* expand_variables(VariableTable* table, const char* input);
void export_variable(VariableTable* table, const char* name);
void set_array_variable(VariableTable* table, const char* name, char** values, int size);
void init_variables();
#endif /* __RICKSHELL_VARIABLE_H__ */