#ifndef AST_H
#define AST_H

#include <stdio.h>

/* типи вузлів AST */
typedef enum {
    N_PROGRAM,
    N_FUNC,       /* str = ім'я, tname = тип результату, kids = [PARAMS, BLOCK] */
    N_PROTO,      /* прототип функції, kids = [PARAMS] */
    N_PARAMS,
    N_PARAM,      /* str = ім'я, tname = тип, ival = 1 якщо масив */
    N_BLOCK,
    N_VARDECL,    /* tname = тип, kids = VAR... */
    N_VAR,        /* str = ім'я, ival = розмір масиву (-1 скаляр, 0 = []), kids = [init?] */
    N_INITLIST,
    N_IF,         /* [cond, then, else?] */
    N_WHILE,      /* [cond, body] */
    N_DOWHILE,    /* [body, cond] */
    N_FOR,        /* [init, cond, step, body], пропущені частини = N_EMPTY */
    N_RETURN,     /* [expr?] */
    N_BREAK,
    N_CONTINUE,
    N_EMPTY,
    N_EXPRSTMT,   /* [expr] */
    N_ASSIGN,     /* str = "=", "+=", ... ; [lhs, rhs] */
    N_BINOP,      /* str = операція */
    N_UNOP,       /* str = "-", "+", "!" */
    N_PREINC,     /* str = "++" або "--" */
    N_POSTINC,
    N_TERNARY,    /* [cond, a, b] */
    N_CALL,       /* str = ім'я, kids = аргументи */
    N_INDEX,      /* [ID, index] */
    N_CAST,       /* tname = тип, [expr] */
    N_ID,
    N_INT,
    N_DOUBLE,
    N_CHAR,       /* ival = код, str = як у тексті ('a') */
    N_STRING      /* str = вміст без лапок (escape-послідовності як є) */
} NodeKind;

/* типи для семантичного аналізу */
typedef enum { T_NONE, T_INT, T_DOUBLE, T_VOID, T_INT_ARR, T_DBL_ARR, T_STR } Type;

struct Symbol;
struct FuncInfo;

typedef struct Node {
    NodeKind kind;
    char *str;
    char *tname;
    long long ival;
    double dval;
    int line;

    struct Node **kids;
    int nkids, cap;

    /* заповнюється семантичним аналізом */
    Type type;
    struct Symbol *sym;
    struct FuncInfo *fn;
} Node;

Node *node_new(NodeKind k, int line);
Node *node_add(Node *parent, Node *child);   /* повертає parent */
Node *mk(NodeKind k, int line, int n, ...);  /* вузол з n дітьми */
Node *mk_str(NodeKind k, int line, const char *s);

const char *kind_name(NodeKind k);
const char *type_name(Type t);
Type type_from_name(const char *tname);

void ast_print(FILE *out, Node *n);  /* текстове дерево */
void ast_json(FILE *out, Node *n);   /* JSON для візуалізації */

/* дрібні утиліти */
char *xstrdup(const char *s);
void *xmalloc(size_t n);

#endif
