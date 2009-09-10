#include "col-internal.h"
#include "parser/ast.h"
#include "types/catalog.h"
#include "types/expr.h"
#include "types/schema.h"

Schema *
schema_make(int len, DataType *types, apr_pool_t *pool)
{
    Schema *schema;

    schema = apr_palloc(pool, sizeof(*schema));
    schema->len = len;
    schema->types = apr_pmemdup(pool, types, len * sizeof(DataType));

    return schema;
}

Schema *
schema_make_from_ast(List *schema_ast, apr_pool_t *pool)
{
    Schema *schema;
    int i;
    ListCell *lc;

    schema = apr_palloc(pool, sizeof(*schema));
    schema->len = list_length(schema_ast);
    schema->types = apr_palloc(pool, schema->len * sizeof(DataType));

    i = 0;
    foreach (lc, schema_ast)
    {
        AstSchemaElt *elt = (AstSchemaElt *) lc_ptr(lc);

        schema->types[i] = get_type_id(elt->type_name);
        i++;
    }

    return schema;
}

Schema *
schema_make_from_exprs(int len, ExprState **expr_ary, apr_pool_t *pool)
{
    Schema *schema;
    int i;

    schema = apr_palloc(pool, sizeof(*schema));
    schema->len = len;
    schema->types = apr_palloc(pool, schema->len * sizeof(DataType));

    for (i = 0; i < schema->len; i++)
    {
        schema->types[i] = expr_ary[i]->expr->type;
    }

    return schema;
}

bool
schema_equal(Schema *s1, Schema *s2)
{
    int i;

    if (s1->len != s2->len)
        return false;

    for (i = 0; i < s1->len; i++)
    {
        if (schema_get_type(s1, i) != schema_get_type(s2, i))
            return false;
    }

    return true;
}
