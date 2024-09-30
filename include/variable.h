#ifndef __RICKSHELL_VARIABLE_H__
#define __RICKSHELL_VARIABLE_H__
#include <stdbool.h>
#include "map.h"

typedef enum {
  VAR_STRING,
  VAR_INTEGER,
  VAR_NAMEREF,
  VAR_ARRAY,            // same as Indexed array
  VAR_ASSOCIATIVE_ARRAY,
} VariableType;

typedef struct {
  union {
    char* _str;
    long long _number;
    double _float;
    map* _map;
    char** _array;
  };
  VariableType type;
} va_value_t;

typedef enum {
  VarFlag_ReadOnly     = 1U << 0,
  VarFlag_Referece     = 1U << 1,
  VarFlag_Exported     = 1U << 2,
  VarFlag_Trace        = 1U << 3,
  VarFlag_TypeDeclared = 1U << 4,
  VarFlag_Uppercase    = 1U << 5,
  VarFlag_Lowercase    = 1U << 6,
} va_flag_t;

bool is_variable_flag_set(va_flag_t* vf, va_flag_t flag);
void set_variable_flag(va_flag_t* vf, va_flag_t flag);
void unset_variable_flag(va_flag_t* vf, va_flag_t flag);

typedef struct {
  char* name;
  char* value;
  VariableType type;
  va_value_t data;
  va_flag_t flags;
  int array_size;
  int array_capacity;
} Variable;

typedef struct {
  Variable* variables;
  int size;
  int capacity;
} VariableTable;

VariableTable* create_variable_table();
void free_variable_table(VariableTable* table);
Variable* set_variable(VariableTable* table, const char* name, const char* value, VariableType type, bool readonly);
Variable* get_variable(VariableTable* table, const char* name);
void unset_variable(VariableTable* table, const char* name);
char* expand_variables(VariableTable* table, const char* input);
bool do_not_expand_this_builtin(const char* name);
VariableType parse_variable_type(const char* value);
Variable* resolve_nameref(Variable* var);
void export_variable(VariableTable* table, const char* name);
void set_array_variable(VariableTable* table, const char* name, char** values, int size);
void init_variables();
void set_associative_array_variable(VariableTable* table, const char* name, const char* key, const char* value);
char* va_value_to_string(const va_value_t* value);
va_value_t string_to_va_value(const char* str, VariableType type);
void free_va_value(va_value_t* value);
#endif /* __RICKSHELL_VARIABLE_H__ */