#ifndef SCAN_H
#define SCAN_H

#include "operator/operator.h"

typedef struct ScanOperator
{
    Operator op;
} ScanOperator;

ScanOperator *scan_op_make(void);

#endif  /* SCAN_H */
