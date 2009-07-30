#ifndef SCAN_H
#define SCAN_H

#include "operator/operator.h"
#include "planner/planner.h"

typedef struct ScanOperator
{
    Operator op;
    ScanPlan *plan;
} ScanOperator;

ScanOperator *scan_op_make(ScanPlan *plan, Operator *next_op,
                           apr_pool_t *pool);

#endif  /* SCAN_H */
