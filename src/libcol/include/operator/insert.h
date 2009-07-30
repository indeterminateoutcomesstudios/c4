#ifndef INSERT_H
#define INSERT_H

#include "operator/operator.h"
#include "planner/planner.h"

typedef struct InsertOperator
{
    Operator op;
    InsertPlan *plan;
} InsertOperator;

InsertOperator *insert_op_make(InsertPlan *plan, apr_pool_t *pool);

#endif  /* INSERT_H */
