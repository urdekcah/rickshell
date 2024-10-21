#ifndef __RICKSHELL_PIPELINE_H__
#define __RICKSHELL_PIPELINE_H__
#include "expr.h"
#include "result.h"

/**
 * @param[in]  first_cmd
 * @param[out] result
 */
IntResult execute_pipeline(Command* first_cmd, int* result);
#endif /* __RICKSHELL_PIPELINE_H__ */