#include <apr_hash.h>

#include "col-internal.h"
#include "types/catalog.h"

struct ColCatalog
{
    ColInstance *col;
    apr_pool_t *pool;

    /* A map from table names => TableDef */
    apr_hash_t *tbl_name_tbl;
};

ColCatalog *
cat_make(ColInstance *col)
{
    apr_pool_t *pool;
    ColCatalog *cat;

    pool = make_subpool(col->pool);
    cat = apr_pcalloc(pool, sizeof(*cat));
    cat->col = col;
    cat->pool = pool;
    cat->tbl_name_tbl = apr_hash_make(cat->pool);

    return cat;
}

void
cat_define_table(ColCatalog *cat, const char *name,
                 List *type_list, List *key_list)
{
    apr_pool_t *tbl_pool;
    TableDef *tbl_def;

    if (cat_get_table(cat, name) != NULL)
        ERROR("Duplicate table definition: %s", name);

    tbl_pool = make_subpool(cat->pool);
    tbl_def = apr_pcalloc(tbl_pool, sizeof(*tbl_def));
    tbl_def->pool = tbl_pool;
    tbl_def->name = apr_pstrdup(tbl_pool, name);
    tbl_def->schema = schema_make_from_list(type_list, tbl_pool);
    tbl_def->key_list = list_copy(key_list, tbl_pool);

    apr_hash_set(cat->tbl_name_tbl, tbl_def->name,
                 APR_HASH_KEY_STRING, tbl_def);
}

void
cat_delete_table(ColCatalog *cat, const char *name)
{
    TableDef *tbl_def;

    tbl_def = cat_get_table(cat, name);
    if (tbl_def == NULL)
        ERROR("No such table: %s", name);

    apr_hash_set(cat->tbl_name_tbl, name, APR_HASH_KEY_STRING, NULL);
    apr_pool_destroy(tbl_def->pool);
}

TableDef *
cat_get_table(ColCatalog *cat, const char *name)
{
    return (TableDef *) apr_hash_get(cat->tbl_name_tbl, name,
                                     APR_HASH_KEY_STRING);
}

bool
is_numeric_type(DataType type_id)
{
    switch (type_id)
    {
        case TYPE_DOUBLE:
        case TYPE_INT2:
        case TYPE_INT4:
        case TYPE_INT8:
            return true;

        default:
            return false;
    }
}

bool
is_valid_type_name(const char *type_name)
{
    return (bool) (get_type_id(type_name) != TYPE_INVALID);
}

DataType
get_type_id(const char *type_name)
{
    if (strcmp(type_name, "bool") == 0)
        return TYPE_BOOL;
    if (strcmp(type_name, "char") == 0)
        return TYPE_CHAR;
    if (strcmp(type_name, "double") == 0)
        return TYPE_DOUBLE;
    if (strcmp(type_name, "int") == 0)
        return TYPE_INT4;
    if (strcmp(type_name, "int2") == 0)
        return TYPE_INT2;
    if (strcmp(type_name, "int4") == 0)
        return TYPE_INT4;
    if (strcmp(type_name, "int8") == 0)
        return TYPE_INT8;
    if (strcmp(type_name, "string") == 0)
        return TYPE_STRING;

    return TYPE_INVALID;
}

char *
get_type_name(DataType type_id)
{
    switch (type_id)
    {
        case TYPE_INVALID:
            return "invalid";
        case TYPE_BOOL:
            return "bool";
        case TYPE_CHAR:
            return "char";
        case TYPE_DOUBLE:
            return "double";
        case TYPE_INT2:
            return "int2";
        case TYPE_INT4:
            return "int4";
        case TYPE_INT8:
            return "int8";
        case TYPE_STRING:
            return "string";

        default:
            ERROR("Unexpected type id: %d", type_id);
            return NULL;        /* Keep compiler quiet */
    }
}
