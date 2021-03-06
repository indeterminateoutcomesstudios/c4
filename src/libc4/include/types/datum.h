#ifndef DATUM_H
#define DATUM_H

#include "util/strbuf.h"

/*
 * This is effectively a set of type IDs. A more sophisticated type ID
 * system is probably a natural next step.
 */
typedef unsigned char DataType;

#define TYPE_INVALID 0
#define TYPE_BOOL    1
#define TYPE_CHAR    2
#define TYPE_DOUBLE  3
#define TYPE_INT     4
#define TYPE_STRING  5

typedef struct C4String
{
    /* The number of bytes in "data"; we do NOT store a NUL terminator */
    apr_uint32_t len;
    apr_uint16_t refcount;
    char data[1];       /* Variable-sized */
} C4String;

typedef union Datum
{
    /* Pass-by-value types (unboxed) */
    bool           b;
    unsigned char  c;
    apr_int64_t    i8;
    double         d8;
    /* Pass-by-ref types (boxed) */
    C4String     *s;
} Datum;

typedef bool (*datum_eq_func)(Datum d1, Datum d2);
typedef int (*datum_cmp_func)(Datum d1, Datum d2);
typedef apr_uint32_t (*datum_hash_func)(Datum d);
typedef Datum (*datum_bin_in_func)(StrBuf *buf);
typedef Datum (*datum_text_in_func)(const char *str);
typedef void (*datum_bin_out_func)(Datum d, StrBuf *buf);
typedef void (*datum_text_out_func)(Datum d, StrBuf *buf);

bool bool_equal(Datum d1, Datum d2);
bool char_equal(Datum d1, Datum d2);
bool double_equal(Datum d1, Datum d2);
bool int_equal(Datum d1, Datum d2);
bool string_equal(Datum d1, Datum d2);

int bool_cmp(Datum d1, Datum d2);
int char_cmp(Datum d1, Datum d2);
int double_cmp(Datum d1, Datum d2);
int int_cmp(Datum d1, Datum d2);
int string_cmp(Datum d1, Datum d2);

apr_uint32_t bool_hash(Datum d);
apr_uint32_t char_hash(Datum d);
apr_uint32_t double_hash(Datum d);
apr_uint32_t int_hash(Datum d);
apr_uint32_t string_hash(Datum d);

/* Binary input functions */
Datum bool_from_buf(StrBuf *buf);
Datum char_from_buf(StrBuf *buf);
Datum double_from_buf(StrBuf *buf);
Datum int_from_buf(StrBuf *buf);
Datum string_from_buf(StrBuf *buf);

/* Binary output functions */
void bool_to_buf(Datum d, StrBuf *buf);
void char_to_buf(Datum d, StrBuf *buf);
void double_to_buf(Datum d, StrBuf *buf);
void int_to_buf(Datum d, StrBuf *buf);
void string_to_buf(Datum d, StrBuf *buf);

/* Text input functions */
Datum bool_from_str(const char *str);
Datum char_from_str(const char *str);
Datum double_from_str(const char *str);
Datum int_from_str(const char *str);
Datum string_from_str(const char *str);

/* Text output functions */
void bool_to_str(Datum d, StrBuf *buf);
void char_to_str(Datum d, StrBuf *buf);
void double_to_str(Datum d, StrBuf *buf);
void int_to_str(Datum d, StrBuf *buf);
void string_to_str(Datum d, StrBuf *buf);

char *string_to_text(Datum d, apr_pool_t *pool);

bool datum_equal(Datum d1, Datum d2, DataType type);
int datum_cmp(Datum d1, Datum d2, DataType type);
Datum datum_copy(Datum d, DataType type);
void datum_free(Datum d, DataType type);
void pool_track_datum(apr_pool_t *pool, Datum datum, DataType type);

void datum_to_str(Datum d, DataType type, StrBuf *buf);
Datum datum_from_str(DataType type, const char *str);

C4String *make_string(apr_size_t slen);

#endif  /* DATUM_H */
