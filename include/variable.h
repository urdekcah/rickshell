#ifndef __RICKSHELL_VARIABLE_H__
#define __RICKSHELL_VARIABLE_H__
#include <stdbool.h>
#include "map.h"
#include "array.h"
#include "rstring.h"

typedef enum {
  VAR_STRING,
  VAR_INTEGER,
  VAR_NAMEREF,
  VAR_ARRAY,            // same as Indexed array
  VAR_ASSOCIATIVE_ARRAY,
} VariableType;

typedef struct {
  union {
    string _str;
    long long _number;
    double _float;
    map* _map;
    array _array;
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

typedef struct {
  string name;
  string str;
  va_value_t value;
  va_flag_t flags;
  int array_size;
  int array_capacity;
} Variable;

typedef struct {
  Variable* variables;
  int size;
  int capacity;
} VariableTable;

bool is_variable_flag_set(va_flag_t* vf, va_flag_t flag);
void set_variable_flag(va_flag_t* vf, va_flag_t flag);
void unset_variable_flag(va_flag_t* vf, va_flag_t flag);

void init_variables();
VariableTable* create_variable_table();
void free_variable_table(VariableTable* table);
Variable* create_new_variable(VariableTable* table, const string name, VariableType type);
void process_string_variable(Variable* var);
void process_exported_variable(Variable* var);
Variable* set_variable(VariableTable* table, const string name, const string value, VariableType type, bool readonly);
Variable* get_variable(VariableTable* table, const string name);
void unset_variable(VariableTable* table, const string name);
void parse_and_set_array(VariableTable* table, const string name, const string value);
void array_set_element(VariableTable* table, const string name, size_t index, const string value);
void parse_and_set_associative_array(VariableTable* table, const string name, const string input);
bool do_not_expand_this_builtin(const string name);
VariableType parse_variable_type(const string value);
Variable* resolve_nameref(Variable* var);
void set_associative_array_variable(VariableTable* table, const string name, const string key, const string value);
string va_value_to_string(const va_value_t* value);
va_value_t string_to_va_value(const string str, VariableType type);
void free_va_value(va_value_t* value);
void vfree_va_value(void* value);
void free_variable(Variable* var);
void cleanup_variables();
char* expand_variables(VariableTable* table, const char* input);
#endif /* __RICKSHELL_VARIABLE_H__ */