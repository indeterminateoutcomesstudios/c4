#include <apr_hash.h>
#include <apr_queue.h>
#include <apr_thread_proc.h>

#include "col-internal.h"
#include "net/network.h"
#include "operator/operator.h"
#include "parser/parser.h"
#include "planner/installer.h"
#include "planner/planner.h"
#include "router.h"
#include "types/catalog.h"
#include "util/list.h"

struct ColRouter
{
    ColInstance *col;
    apr_pool_t *pool;

    /* Map from table name => OpChain list */
    apr_hash_t *delta_tbl;

    /* Thread info for router thread */
    apr_threadattr_t *thread_attr;
    apr_thread_t *thread;

    /* Queue of to-be-routed tuples; accessed by other threads */
    apr_queue_t *queue;

    int ntuple_routed;
};

typedef enum WorkItemKind
{
    WI_TUPLE,
    WI_PROGRAM,
    WI_SHUTDOWN
} WorkItemKind;

typedef struct WorkItem
{
    WorkItemKind kind;

    /* WI_TUPLE: */
    Tuple *tuple;
    char *tbl_name;

    /* WI_PROGRAM: */
    char *program_src;
} WorkItem;

static void * APR_THREAD_FUNC router_thread_start(apr_thread_t *thread, void *data);
static void router_enqueue(ColRouter *router, WorkItem *wi);

ColRouter *
router_make(ColInstance *col)
{
    apr_status_t s;
    apr_pool_t *pool;
    ColRouter *router;

    pool = make_subpool(col->pool);
    router = apr_pcalloc(pool, sizeof(*router));
    router->col = col;
    router->pool = pool;
    router->delta_tbl = apr_hash_make(pool);
    router->ntuple_routed = 0;

    s = apr_queue_create(&router->queue, 128, router->pool);
    if (s != APR_SUCCESS)
        FAIL();

    return router;
}

void
router_destroy(ColRouter *router)
{
    apr_status_t s;

    s = apr_queue_term(router->queue);
    if (s != APR_SUCCESS)
        FAIL();

    apr_pool_destroy(router->pool);
}

void
router_start(ColRouter *router)
{
    apr_status_t s;

    s = apr_threadattr_create(&router->thread_attr, router->pool);
    if (s != APR_SUCCESS)
        FAIL();

    s = apr_thread_create(&router->thread, router->thread_attr,
                          router_thread_start, router, router->pool);
    if (s != APR_SUCCESS)
        FAIL();
}

/*
 * Returns true if the tuple is remote and we've enqueued it for network
 * send; false otherwise.
 */
static bool
route_tuple_remote(ColRouter *router, Tuple *tuple, const char *tbl_name)
{
    TableDef *tbl_def;

    tbl_def = cat_get_table(router->col->cat, tbl_name);
    if (tbl_def == NULL)
        FAIL();

    if (tbl_def->ls_colno != -1)
    {
        Datum tuple_addr;

        tuple_addr = tuple_get_val(tuple, tbl_def->ls_colno);
        if (!datum_equal(router->col->local_addr, tuple_addr, TYPE_STRING))
        {
            col_log(router->col, "Remote tuple: %s => %s",
                    log_tuple(router->col, tuple),
                    log_datum(router->col, tuple_addr, TYPE_STRING));
            network_send(router->col->net, tuple_addr, tbl_name, tuple);
            return true;
        }
    }

    return false;
}

/*
 * Route a new tuple. If the tuple is remote (i.e. the specified table has a
 * location specifier that denotes an address other than the local node
 * address), then we enqueue the tuple in the appropriate network buffer.
 * Otherwise, we route the tuple locally (pass it to the appropriate
 * operator chains).
 */
static void
route_tuple(ColRouter *router, Tuple *tuple, const char *tbl_name)
{
    OpChain *op_chain;

    col_log(router->col, "%s: %s",
            __func__, log_tuple(router->col, tuple));

    if (route_tuple_remote(router, tuple, tbl_name))
        return;

    op_chain = apr_hash_get(router->delta_tbl, tbl_name,
                            APR_HASH_KEY_STRING);
    while (op_chain != NULL)
    {
        Operator *start = op_chain->chain_start;
        start->invoke(start, tuple);
        op_chain = op_chain->next;
    }

    router->ntuple_routed++;
}

static void
route_program(ColRouter *router, const char *src)
{
    ColInstance *col = router->col;
    apr_pool_t *program_pool;
    AstProgram *ast;
    ProgramPlan *plan;

    program_pool = make_subpool(col->pool);

    ast = parse_str(src, program_pool, col);
    plan = plan_program(ast, program_pool, col);
    install_plan(plan, program_pool, col);

    apr_pool_destroy(program_pool);
}

static void
workitem_destroy(WorkItem *wi)
{
    switch (wi->kind)
    {
        case WI_TUPLE:
            tuple_unpin(wi->tuple);
            ol_free(wi->tbl_name);
            break;

        case WI_PROGRAM:
            ol_free(wi->program_src);
            break;

        case WI_SHUTDOWN:
            break;

        default:
            ERROR("Unrecognized WorkItem kind: %d", (int) wi->kind);
    }

    ol_free(wi);
}

static void * APR_THREAD_FUNC
router_thread_start(apr_thread_t *thread, void *data)
{
    ColRouter *router = (ColRouter *) data;

    while (true)
    {
        apr_status_t s;
        WorkItem *wi;

        s = apr_queue_pop(router->queue, (void **) &wi);

        if (s == APR_EINTR)
            continue;
        if (s == APR_EOF)
            break;
        if (s != APR_SUCCESS)
            FAIL();

        switch (wi->kind)
        {
            case WI_TUPLE:
                route_tuple(router, wi->tuple, wi->tbl_name);
                break;

            case WI_PROGRAM:
                route_program(router, wi->program_src);
                break;

            case WI_SHUTDOWN:
                break;

            default:
                ERROR("Unrecognized WorkItem kind: %d", (int) wi->kind);
        }

        workitem_destroy(wi);
    }

    apr_thread_exit(thread, APR_SUCCESS);
    return NULL;        /* Return value ignored */
}

void
router_enqueue_program(ColRouter *router, const char *src)
{
    WorkItem *wi;

    wi = ol_alloc0(sizeof(*wi));
    wi->kind = WI_PROGRAM;
    wi->program_src = ol_strdup(src);

    router_enqueue(router, wi);
}

void
router_enqueue_tuple(ColRouter *router, Tuple *tuple,
                     const char *tbl_name)
{
    WorkItem *wi;

    col_log(router->col, "%s: %s",
            __func__, log_tuple(router->col, tuple));

    /* The tuple is unpinned when the router is finished with it */
    tuple_pin(tuple);

    wi = ol_alloc0(sizeof(*wi));
    wi->kind = WI_TUPLE;
    wi->tuple = tuple;
    wi->tbl_name = ol_strdup(tbl_name);

    router_enqueue(router, wi);
}

/*
 * Enqueue a new WorkItem to be routed. The WorkItem will be routed in some
 * subsequent fixpoint. WorkItems are routed in the order in which they are
 * enqueued. If the queue is full, this function blocks until the router has
 * had a chance to catchup.
 */
static void
router_enqueue(ColRouter *router, WorkItem *wi)
{
    while (true)
    {
        apr_status_t s = apr_queue_push(router->queue, wi);

        if (s == APR_EINTR)
            continue;
        if (s == APR_SUCCESS)
            break;

        FAIL();
    }
}

void
router_add_op_chain(ColRouter *router, OpChain *op_chain)
{
    OpChain *old_chain;

    ASSERT(op_chain->next == NULL);
    old_chain = apr_hash_get(router->delta_tbl, op_chain->delta_tbl,
                             APR_HASH_KEY_STRING);
    op_chain->next = old_chain;
    apr_hash_set(router->delta_tbl, op_chain->delta_tbl,
                 APR_HASH_KEY_STRING, op_chain);
}
