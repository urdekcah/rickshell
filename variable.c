#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "variable.h"
#include "memory.h"
#include "error.h"

#define INITIAL_TABLE_SIZE 10

VariableTable* variable_table = NULL;

void init_variables() {
  variable_table = create_variable_table();
}

VariableTable* create_variable_table() {
  VariableTable* table = rmalloc(sizeof(VariableTable));
  table->variables = rmalloc(INITIAL_TABLE_SIZE * sizeof(Variable));
  table->size = 0;
  table->capacity = INITIAL_TABLE_SIZE;
  return table;
}

void free_variable_table(VariableTable* table) {
  for (int i = 0; i < table->size; i++) {
    rfree(table->variables[i].name);
    rfree(table->variables[i].value);
    if (table->variables[i].is_array) {
      for (int j = 0; j < table->variables[i].array_size; j++) {
        rfree(table->variables[i].array_values[j]);
      }
      rfree(table->variables[i].array_values);
    }
  }
  rfree(table->variables);
  rfree(table);
}

static void expand_table(VariableTable* table) {
  table->capacity *= 2;
  table->variables = rrealloc(table->variables, table->capacity * sizeof(Variable));
}

Variable* set_variable(VariableTable* table, const char* name, const char* value, bool readonly) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      if (table->variables[i].readonly) {
        print_error("Cannot modify readonly variable");
        return NULL;
      }
      rfree(table->variables[i].value);
      table->variables[i].value = rstrdup(value);
      table->variables[i].readonly = readonly;
      return &table->variables[i];
    }
  }

  if (table->size == table->capacity) {
    expand_table(table);
  }

  Variable* var = &table->variables[table->size++];
  var->name = rstrdup(name);
  var->value = rstrdup(value);
  var->readonly = readonly;
  var->is_array = false;
  var->array_values = NULL;
  var->array_size = 0;

  return var;
}

Variable* get_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      return &table->variables[i];
    }
  }
  return NULL;
}

void unset_variable(VariableTable* table, const char* name) {
  for (int i = 0; i < table->size; i++) {
    if (strcmp(table->variables[i].name, name) == 0) {
      if (table->variables[i].readonly) {
        print_error("Cannot unset readonly variable");
        return;
      }
      rfree(table->variables[i].name);
      rfree(table->variables[i].value);
      if (table->variables[i].is_array) {
        for (int j = 0; j < table->variables[i].array_size; j++) {
          rfree(table->variables[i].array_values[j]);
        }
        rfree(table->variables[i].array_values);
      }
      memmove(&table->variables[i], &table->variables[i + 1], (table->size - i - 1) * sizeof(Variable));
      table->size--;
      return;
    }
  }
}

char* expand_variables(VariableTable* table, const char* input) {
  size_t result_size = strlen(input) * 2;
  char* result = rmalloc(result_size);
  char* output = result;
  const char* p = input;

  while (*p) {
    if (*p == '$') {
      if (*(p + 1) == '{') {
        const char* end = strchr(p + 2, '}');
        if (end) {
          size_t var_name_len = end - (p + 2);
          char* var_name = rmalloc(var_name_len + 1);
          strncpy(var_name, p + 2, var_name_len);
          var_name[var_name_len] = '\0';
          Variable* var = get_variable(table, var_name);
          if (var) {
            size_t value_len = strlen(var->value);
            if (output - result + value_len >= result_size) {
              size_t current_len = output - result;
              result_size *= 2;
              result = rrealloc(result, result_size);
              output = result + current_len;
            }
            strcpy(output, var->value);
            output += value_len;
          }
          rfree(var_name);
          p = end + 1;
          continue;
        }
      } else if (isalnum(*(p + 1)) || *(p + 1) == '_') {
        const char* end = p + 1;
        while (isalnum(*end) || *end == '_') end++;
        size_t var_name_len = end - (p + 1);
        char* var_name = rmalloc(var_name_len + 1);
        strncpy(var_name, p + 1, var_name_len);
        var_name[var_name_len] = '\0';
        Variable* var = get_variable(table, var_name);
        if (var) {
          size_t value_len = strlen(var->value);
          if (output - result + value_len >= result_size) {
            size_t current_len = output - result;
            result_size *= 2;
            result = rrealloc(result, result_size);
            output = result + current_len;
          }
          strcpy(output, var->value);
          output += value_len;
        }
        rfree(var_name);
        p = end;
        continue;
      }
    }
    if (output - result + 1 >= (long int)result_size) {
      size_t current_len = output - result;
      result_size *= 2;
      result = rrealloc(result, result_size);
      output = result + current_len;
    }
    *output++ = *p++;
  }
  *output = '\0';

  return result;
}

void export_variable(VariableTable* table, const char* name) {
  Variable* var = get_variable(table, name);
  if (var) {
    setenv(name, var->value, 1);
  } else {
    print_error("Variable not found");
  }
}

void set_array_variable(VariableTable* table, const char* name, char** values, int size) {
  Variable* var = get_variable(table, name);
  if (var) {
    if (var->readonly) {
      print_error("Cannot modify readonly variable");
      return;
    }
    if (var->is_array) {
      for (int i = 0; i < var->array_size; i++) {
        rfree(var->array_values[i]);
      }
      rfree(var->array_values);
    }
  } else {
    var = set_variable(table, name, "", false);
  }

  var->is_array = true;
  var->array_values = rmalloc(size * sizeof(char*));
  var->array_size = size;

  for (int i = 0; i < size; i++) {
    var->array_values[i] = rstrdup(values[i]);
  }
}