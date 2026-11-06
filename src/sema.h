#ifndef SEMA_H
#define SEMA_H

#include "ast.h"

/* змінна (або параметр) після розв'язання імен */
typedef struct Symbol {
    char *name;      /* ім'я в C */
    char *pyname;    /* унікальне ім'я для генерованого Python */
    Type type;
    int is_global;
    int slot;        /* номер комірки у фреймі (або в масиві глобальних) */
    int line;
} Symbol;

typedef enum { B_NONE, B_PRINTF, B_PUTCHAR, B_ABS, B_FABS, B_SQRT, B_POW } Builtin;

typedef struct FuncInfo {
    char *name;
    char *pyname;
    Type ret;
    int nparams;
    Type ptypes[32];
    Symbol *params[32];
    Node *def;       /* N_FUNC або NULL, якщо є тільки прототип */
    int nslots;      /* скільки локальних комірок треба фрейму */
    Builtin builtin;
    int variadic;
    struct FuncInfo *next;
} FuncInfo;

extern int nglobals;          /* кількість глобальних комірок */
extern FuncInfo *functions;   /* список усіх функцій (разом з вбудованими) */

int sema_check(Node *program);  /* повертає кількість помилок */
FuncInfo *find_func(const char *name);

#endif
