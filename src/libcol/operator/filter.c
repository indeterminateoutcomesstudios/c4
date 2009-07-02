#include "col-internal.h"
#include "operator/filter.h"

static void
filter_invoke(Operator *op, Tuple *t)
{
    FilterOperator *filt_op = (FilterOperator *) op;
}

static void
filter_destroy(Operator *op)
{
    operator_destroy(op);
}

FilterOperator *
filter_op_make(apr_pool_t *pool)
{
    FilterOperator *filter_op;

    filter_op = (FilterOperator *) operator_make(OPER_FILTER,
                                                 sizeof(*filter_op),
                                                 filter_invoke,
                                                 filter_destroy,
                                                 pool);

    return filter_op;
}
