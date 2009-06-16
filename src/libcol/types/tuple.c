#include <apr_atomic.h>

#include "col-internal.h"
#include "types/tuple.h"
#include "util/socket.h"

Tuple *
tuple_make(void)
{
    Tuple *t = (Tuple *) ol_alloc(sizeof(Tuple));
    t->refcount = 1;
    return t;
}

void
tuple_pin(Tuple *tuple)
{
    apr_uint32_t old_count = apr_atomic_inc32(&tuple->refcount);
    ASSERT(old_count > 0);
}

void
tuple_unpin(Tuple *tuple)
{
    if (apr_atomic_dec32(&tuple->refcount) == 0)
        ol_free(tuple);
}

void
tuple_socket_send(Tuple *tuple, apr_socket_t *sock)
{
    socket_send_str(sock, "hello world");
}

Tuple *
tuple_from_buf(const char *buf, apr_size_t len)
{
    return NULL;
}
