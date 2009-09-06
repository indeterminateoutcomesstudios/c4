#ifndef INSERT_H
#define INSERT_H

#include "operator/operator.h"
#include "planner/planner.h"

typedef struct InsertOperator
{
    Operator op;
} InsertOperator;

InsertOperator *insert_op_make(InsertPlan *plan, OpChain *chain);

#endif  /* INSERT_H */
