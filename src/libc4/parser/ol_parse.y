%{
/* XXX: should be a pure-parser */
#include "c4-internal.h"
#include "nodes/makefuncs.h"
/* XXX: see note about #include order in parser.c */
#include "parser/parser-internal.h"
#include "ol_parse.h"
#include "ol_scan.h"
#include "util/list.h"

int yyerror(C4Parser *context, void *scanner, const char *message);
static void split_program_clauses(List *clauses, List **defines, List **timers,
                                  List **facts, List **rules, apr_pool_t *pool);
static void split_rule_body(List *body, List **joins,
                            List **quals, apr_pool_t *pool);
%}

%union
{
    apr_int64_t     ival;
    char           *str;
    List           *list;
    void           *ptr;
    bool            boolean;
    AstAggKind      agg_kind;
}

%start program
%error-verbose
%pure-parser
%parse-param { C4Parser *context }
%parse-param { void *scanner }
%lex-param { yyscan_t scanner }

%token DEFINE MEMORY SQLITE DELETE NOTIN TIMER
       OL_FALSE OL_TRUE OL_AVG OL_COUNT OL_MAX OL_MIN OL_SUM
%token <str> VAR_IDENT TBL_IDENT FCONST SCONST CCONST ICONST

%left OL_EQ OL_NEQ
%nonassoc '>' '<' OL_GTE OL_LTE
%left '+' '-'
%left '*' '/' '%'
%left UMINUS

%type <ptr>        clause define rule timer table_ref join_clause
%type <list>       program_body schema_list define_schema
%type <list>       expr_list opt_rule_body rule_body
%type <ptr>        rule_body_elem qualifier qual_expr expr const_expr op_expr
%type <ptr>        var_expr agg_expr rule_prefix schema_elt
%type <agg_kind>   agg_kind
%type <str>        bool_const
%type <ival>       iconst_ival
%type <boolean>    opt_not opt_delete

%%
program: program_body {
    List *defines;
    List *timers;
    List *facts;
    List *rules;

    split_program_clauses($1, &defines, &timers, &facts, &rules, context->pool);
    context->result = make_program(defines, timers, facts, rules, context->pool);
};

program_body:
  clause ';' program_body       { $$ = list_prepend($3, $1); }
| /* EMPTY */                   { $$ = list_make(context->pool); }
;

clause:
  define        { $$ = $1; }
| timer         { $$ = $1; }
| rule          { $$ = $1; }
;

/*
 * XXX: We need to write this in a non-obvious way because bison gets
 * confused by adjacent optional productions. Is there a better fix?
 */
define:
  DEFINE '(' TBL_IDENT ',' MEMORY ',' define_schema ')' {
    $$ = make_define($3, AST_STORAGE_MEMORY, $7, context->pool);
}
| DEFINE '(' TBL_IDENT ',' SQLITE ',' define_schema ')' {
    $$ = make_define($3, AST_STORAGE_SQLITE, $7, context->pool);
}
| DEFINE '(' TBL_IDENT ',' define_schema ')' {
    $$ = make_define($3, AST_STORAGE_MEMORY, $5, context->pool);
}
;

timer: TIMER '(' TBL_IDENT ',' iconst_ival ')' {
    $$ = make_ast_timer($3, $5, context->pool);
}
;

define_schema: '{' schema_list '}' { $$ = $2; };

/*
 * This is identical to ICONST, except that we convert the string returned
 * by the lexer into an integer. Note that this conversion might fail,
 * because the integer is malformed (e.g. too large).
 */
iconst_ival: ICONST {
    apr_int64_t ival;
    char *endptr;

    errno = 0;
    ival = strtol($1, &endptr, 10);
    if (errno != 0 || endptr[0] != '\0')
        FAIL();

    $$ = ival;
};

schema_list:
  schema_elt                  { $$ = list_make1($1, context->pool); }
| schema_list ',' schema_elt  { $$ = list_append($1, $3); }
;

schema_elt:
  '@' TBL_IDENT               { $$ = make_schema_elt($2, true, context->pool); }
| TBL_IDENT                   { $$ = make_schema_elt($1, false, context->pool); }
;

rule: rule_prefix opt_rule_body {
    AstRule *rule = (AstRule *) $1;

    if ($2 == NULL)
    {
        /* A rule without a body is actually a fact */
        if (rule->name != NULL)
            ERROR("Cannot assign a name to facts");
        if (rule->is_delete)
            ERROR("Cannot specify \"delete\" in a fact");

        $$ = make_fact(rule->head, context->pool);
    }
    else
    {
        List *joins;
        List *quals;

        split_rule_body($2, &joins, &quals, context->pool);
        rule->joins = joins;
        rule->quals = quals;
        $$ = rule;
    }
};

rule_prefix:
  TBL_IDENT opt_delete table_ref { $$ = make_rule($1, $2, false, false, $3, NULL, NULL, context->pool); }
| DELETE table_ref               { $$ = make_rule(NULL, true, false, false, $2, NULL, NULL, context->pool); }
| table_ref                      { $$ = make_rule(NULL, false, false, false, $1, NULL, NULL, context->pool); }
;

opt_delete:
  DELETE        { $$ = true; }
| /* EMPTY */   { $$ = false; }
;

opt_rule_body:
  ':' '-' rule_body     { $$ = $3; }
| /* EMPTY */           { $$ = NULL; }
;

rule_body:
  rule_body_elem                { $$ = list_make1($1, context->pool); }
| rule_body ',' rule_body_elem  { $$ = list_append($1, $3); }
;

rule_body_elem:
  join_clause
| qualifier
;

join_clause: opt_not TBL_IDENT '(' expr_list ')' {
    AstTableRef *ref = make_table_ref($2, $4, context->pool);
    $$ = make_join_clause(ref, $1, context->pool);
};

opt_not:
  NOTIN                 { $$ = true; }
| /* EMPTY */           { $$ = false; }
;

table_ref: TBL_IDENT '(' expr_list ')' { $$ = make_table_ref($1, $3, context->pool); };

qualifier: qual_expr { $$ = make_qualifier($1, context->pool); };

expr:
  op_expr
| const_expr
| var_expr
| agg_expr
;

op_expr:
  expr '+' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_PLUS, context->pool); }
| expr '-' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_MINUS, context->pool); }
| expr '*' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_TIMES, context->pool); }
| expr '/' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_DIVIDE, context->pool); }
| expr '%' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_MODULUS, context->pool); }
| '-' expr              { $$ = make_ast_op_expr($2, NULL, AST_OP_UMINUS, context->pool); }
| qual_expr             { $$ = $1; }
;

qual_expr:
  expr '<' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_LT, context->pool); }
| expr '>' expr         { $$ = make_ast_op_expr($1, $3, AST_OP_GT, context->pool); }
| expr OL_LTE expr      { $$ = make_ast_op_expr($1, $3, AST_OP_LTE, context->pool); }
| expr OL_GTE expr      { $$ = make_ast_op_expr($1, $3, AST_OP_GTE, context->pool); }
| expr OL_EQ expr       { $$ = make_ast_op_expr($1, $3, AST_OP_EQ, context->pool); }
| expr OL_NEQ expr      { $$ = make_ast_op_expr($1, $3, AST_OP_NEQ, context->pool); }
;

const_expr:
  bool_const { $$ = make_ast_const_expr(AST_CONST_BOOL, $1, context->pool); }
| ICONST { $$ = make_ast_const_expr(AST_CONST_INT, $1, context->pool); }
| FCONST { $$ = make_ast_const_expr(AST_CONST_DOUBLE, $1, context->pool); }
| SCONST { $$ = make_ast_const_expr(AST_CONST_STRING, $1, context->pool); }
| CCONST { $$ = make_ast_const_expr(AST_CONST_CHAR, $1, context->pool); }
;

bool_const:
  OL_TRUE       { $$ = apr_pstrdup(context->pool, "true"); }
| OL_FALSE      { $$ = apr_pstrdup(context->pool, "false"); }
;

var_expr: VAR_IDENT { $$ = make_ast_var_expr($1, TYPE_INVALID, context->pool); };

agg_expr: agg_kind '<' expr '>' { $$ = make_ast_agg_expr($1, $3, context->pool); }

agg_kind:
  OL_AVG        { $$ = AST_AGG_AVG; }
| OL_COUNT      { $$ = AST_AGG_COUNT; }
| OL_MAX        { $$ = AST_AGG_MAX; }
| OL_MIN        { $$ = AST_AGG_MIN; }
| OL_SUM        { $$ = AST_AGG_SUM; }
;

expr_list:
  expr { $$ = list_make1($1, context->pool); }
| expr_list ',' expr { $$ = list_append($1, $3); }
;

%%

int
yyerror(C4Parser *context, void *scanner, const char *message)
{
    printf("Parse error: %s\n", message);
    return 0;   /* return value ignored */
}

static void
split_program_clauses(List *clauses, List **defines, List **timers,
                      List **facts, List **rules, apr_pool_t *pool)
{
    ListCell *lc;

    *defines = list_make(pool);
    *timers = list_make(pool);
    *facts = list_make(pool);
    *rules = list_make(pool);

    foreach (lc, clauses)
    {
        C4Node *node = (C4Node *) lc_ptr(lc);

        switch (node->kind)
        {
            case AST_DEFINE:
                list_append(*defines, node);
                break;

            case AST_TIMER:
                list_append(*timers, node);
                break;

            case AST_FACT:
                list_append(*facts, node);
                break;

            case AST_RULE:
                list_append(*rules, node);
                break;

            default:
                ERROR("Unexpected node kind: %d", (int) node->kind);
        }
    }
}

/*
 * Split the rule body into join clauses and qualifiers, and store each in
 * its own list for subsequent ease of processing. Note that this implies
 * that the relative order of join clauses and qualifiers is not
 * semantically significant.
 */
static void
split_rule_body(List *body, List **joins, List **quals, apr_pool_t *pool)
{
    ListCell *lc;

    *joins = list_make(pool);
    *quals = list_make(pool);

    foreach (lc, body)
    {
        C4Node *node = (C4Node *) lc_ptr(lc);

        switch (node->kind)
        {
            case AST_JOIN_CLAUSE:
                list_append(*joins, node);
                break;

            case AST_QUALIFIER:
                list_append(*quals, node);
                break;

            default:
                ERROR("Unexpected node kind: %d", (int) node->kind);
        }
    }
}
