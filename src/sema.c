/*
 * Семантичний аналіз:
 *   - таблиця символів з вкладеними областями видимості;
 *   - перевірка оголошень/використань, кількості аргументів, lvalue;
 *   - виведення типів виразів (int / double / масиви);
 *   - кожній змінній призначається комірка (slot) і унікальне ім'я для Python.
 */
#include <stdlib.h>
#include <string.h>
#include "sema.h"

int nglobals = 0;
FuncInfo *functions = NULL;

static int errors = 0;

static void error(int line, const char *fmt, const char *a, const char *b) {
    fprintf(stderr, "line %d: error: ", line);
    fprintf(stderr, fmt, a ? a : "", b ? b : "");
    fputc('\n', stderr);
    errors++;
}

/* ---------- області видимості ---------- */

#define MAX_SCOPE 128
#define MAX_SYMS  512

typedef struct { Symbol *syms[MAX_SYMS]; int n; } Scope;
static Scope scopes[MAX_SCOPE];
static int depth = -1;

static void push_scope(void) { scopes[++depth].n = 0; }
static void pop_scope(void) { depth--; }

static Symbol *lookup(const char *name) {
    for (int d = depth; d >= 0; d--)
        for (int i = scopes[d].n - 1; i >= 0; i--)
            if (!strcmp(scopes[d].syms[i]->name, name)) return scopes[d].syms[i];
    return NULL;
}

static Symbol *lookup_current(const char *name) {
    for (int i = 0; i < scopes[depth].n; i++)
        if (!strcmp(scopes[depth].syms[i]->name, name)) return scopes[depth].syms[i];
    return NULL;
}

/* ---------- імена для Python ---------- */

static const char *PY_RESERVED[] = {
    "False", "None", "True", "and", "as", "assert", "async", "await", "class", "def",
    "del", "elif", "except", "finally", "from", "global", "import", "in", "is",
    "lambda", "nonlocal", "not", "or", "pass", "raise", "try", "with", "yield",
    "print", "int", "float", "abs", "ord", "range", "len", "list", "str", "math",
    "sys", "input", "min", "max", "sum", "match", "case", "type", "id", NULL
};

/* імена, вже зайняті в поточній функції (+ глобальні) */
static char *used_names[2048];
static int nused = 0, nused_globals = 0;

static int name_used(const char *s) {
    for (int i = 0; i < nused; i++) if (!strcmp(used_names[i], s)) return 1;
    return 0;
}

static char *make_pyname(const char *name) {
    char buf[300];
    snprintf(buf, sizeof buf, "%s", name);
    for (int i = 0; PY_RESERVED[i]; i++)
        if (!strcmp(PY_RESERVED[i], name)) { snprintf(buf, sizeof buf, "%s_", name); break; }
    if (name[0] == '_') snprintf(buf, sizeof buf, "v%s", name);  /* _idiv і т.п. зайняті хелперами */
    char base[256];
    strcpy(base, buf);
    for (int k = 2; name_used(buf); k++) snprintf(buf, sizeof buf, "%s_%d", base, k);
    used_names[nused++] = xstrdup(buf);
    return used_names[nused - 1];
}

/* ---------- функції ---------- */

FuncInfo *find_func(const char *name) {
    for (FuncInfo *f = functions; f; f = f->next)
        if (!strcmp(f->name, name)) return f;
    return NULL;
}

static FuncInfo *add_builtin(const char *name, Builtin b, Type ret, int n, Type t1, Type t2) {
    FuncInfo *f = xmalloc(sizeof *f);
    f->name = xstrdup(name);
    f->builtin = b;
    f->ret = ret;
    f->nparams = n;
    f->ptypes[0] = t1;
    f->ptypes[1] = t2;
    f->next = functions;
    functions = f;
    return f;
}

static FuncInfo *cur_func = NULL;
static int loop_depth = 0;

/* ---------- вирази ---------- */

static int is_scalar(Type t) { return t == T_INT || t == T_DOUBLE; }
static Type elem_type(Type t) { return t == T_DBL_ARR ? T_DOUBLE : T_INT; }
static int is_lvalue(Node *n) { return (n->kind == N_ID && n->sym && is_scalar(n->type)) || n->kind == N_INDEX; }

static void expr(Node *n);

static void need_scalar(Node *n, const char *what) {
    if (n->type == T_NONE) return;  /* помилку вже повідомили */
    if (!is_scalar(n->type))
        error(n->line, "%s must be a number (got %s)", what, type_name(n->type));
}

static void check_call(Node *n) {
    FuncInfo *f = find_func(n->str);
    for (int i = 0; i < n->nkids; i++) expr(n->kids[i]);
    if (!f) { error(n->line, "call to undeclared function '%s'", n->str, NULL); n->type = T_INT; return; }
    n->fn = f;
    n->type = f->ret;

    if (f->builtin == B_PRINTF) {
        if (n->nkids == 0 || n->kids[0]->kind != N_STRING)
            error(n->line, "printf: first argument must be a string literal%s", "", NULL);
        for (int i = 1; i < n->nkids; i++)
            if (!is_scalar(n->kids[i]->type) && n->kids[i]->type != T_STR)
                error(n->line, "printf: bad argument type %s", type_name(n->kids[i]->type), NULL);
        return;
    }
    if (n->nkids != f->nparams) {
        char buf[64];
        snprintf(buf, sizeof buf, "%d, got %d", f->nparams, n->nkids);
        error(n->line, "function '%s' expects %s arguments", n->str, buf);
        return;
    }
    for (int i = 0; i < n->nkids; i++) {
        Type want = f->ptypes[i], got = n->kids[i]->type;
        if (got == T_NONE) continue;
        if (is_scalar(want) && !is_scalar(got))
            error(n->kids[i]->line, "argument of '%s' must be a number, got %s", n->str, type_name(got));
        if (!is_scalar(want) && want != got)
            error(n->kids[i]->line, "argument of '%s' must be an array %s", n->str, type_name(want));
    }
}

static void expr(Node *n) {
    switch (n->kind) {
    case N_INT: case N_CHAR: n->type = T_INT; break;
    case N_DOUBLE: n->type = T_DOUBLE; break;
    case N_STRING: n->type = T_STR; break;

    case N_ID: {
        Symbol *s = lookup(n->str);
        if (!s) {
            error(n->line, "'%s' undeclared", n->str, NULL);
            n->type = T_NONE;
        } else {
            n->sym = s;
            n->type = s->type;
        }
        break;
    }
    case N_INDEX: {
        Node *a = n->kids[0], *i = n->kids[1];
        expr(a); expr(i);
        if (a->type != T_NONE && a->type != T_INT_ARR && a->type != T_DBL_ARR)
            error(n->line, "'%s' is not an array", a->str, NULL);
        if (i->type == T_DOUBLE) error(n->line, "array index must be an integer%s", "", NULL);
        else need_scalar(i, "array index");
        n->type = elem_type(a->type);
        break;
    }
    case N_CALL: check_call(n); break;

    case N_BINOP: {
        Node *a = n->kids[0], *b = n->kids[1];
        expr(a); expr(b);
        need_scalar(a, "operand");
        need_scalar(b, "operand");
        const char *op = n->str;
        if (!strcmp(op, "+") || !strcmp(op, "-") || !strcmp(op, "*") || !strcmp(op, "/"))
            n->type = (a->type == T_DOUBLE || b->type == T_DOUBLE) ? T_DOUBLE : T_INT;
        else if (!strcmp(op, "%")) {
            if (a->type == T_DOUBLE || b->type == T_DOUBLE)
                error(n->line, "operator %% needs integer operands%s", "", NULL);
            n->type = T_INT;
        } else n->type = T_INT;   /* порівняння, && , || */
        break;
    }
    case N_UNOP:
        expr(n->kids[0]);
        need_scalar(n->kids[0], "operand");
        n->type = !strcmp(n->str, "!") ? T_INT : n->kids[0]->type;
        break;

    case N_PREINC: case N_POSTINC:
        expr(n->kids[0]);
        if (!is_lvalue(n->kids[0])) error(n->line, "operand of %s must be a variable", n->str, NULL);
        n->type = n->kids[0]->type;
        break;

    case N_ASSIGN: {
        Node *l = n->kids[0], *r = n->kids[1];
        expr(l); expr(r);
        if (l->type != T_NONE && !is_lvalue(l)) error(n->line, "left side of '%s' is not assignable", n->str, NULL);
        need_scalar(r, "assigned value");
        if (!strcmp(n->str, "%=") && (l->type == T_DOUBLE || r->type == T_DOUBLE))
            error(n->line, "operator %%= needs integer operands%s", "", NULL);
        n->type = l->type;
        break;
    }
    case N_TERNARY:
        for (int i = 0; i < 3; i++) expr(n->kids[i]);
        need_scalar(n->kids[0], "condition of ?:");
        if (n->kids[1]->type == T_STR && n->kids[2]->type == T_STR) {   /* cond ? "a" : "b" для printf */
            n->type = T_STR;
            break;
        }
        need_scalar(n->kids[1], "operand of ?:");
        need_scalar(n->kids[2], "operand of ?:");
        n->type = (n->kids[1]->type == T_DOUBLE || n->kids[2]->type == T_DOUBLE) ? T_DOUBLE : T_INT;
        break;

    case N_CAST:
        expr(n->kids[0]);
        need_scalar(n->kids[0], "cast operand");
        n->type = type_from_name(n->tname);
        if (n->type == T_VOID) error(n->line, "cast to void is not supported%s", "", NULL);
        break;

    default:
        error(n->line, "unexpected node %s in expression", kind_name(n->kind), NULL);
    }
}

/* ---------- оголошення ---------- */

static Symbol *declare(const char *name, Type t, int line, int global) {
    if (lookup_current(name)) {
        error(line, "redeclaration of '%s'", name, NULL);
        return NULL;
    }
    if (scopes[depth].n >= MAX_SYMS) { error(line, "too many variables%s", "", NULL); return NULL; }
    Symbol *s = xmalloc(sizeof *s);
    s->name = xstrdup(name);
    s->type = t;
    s->line = line;
    s->is_global = global;
    if (global) {
        s->slot = nglobals++;
        s->pyname = make_pyname(name);
    } else {
        s->slot = cur_func->nslots++;
        s->pyname = make_pyname(name);
    }
    scopes[depth].syms[scopes[depth].n++] = s;
    return s;
}

static void vardecl(Node *d, int global) {
    Type base = type_from_name(d->tname);
    if (base == T_VOID) { error(d->line, "variable declared void%s", "", NULL); return; }
    for (int i = 0; i < d->nkids; i++) {
        Node *v = d->kids[i];
        Node *init = v->nkids ? v->kids[0] : NULL;
        int is_arr = v->ival >= 0;
        Type t = is_arr ? (base == T_DOUBLE ? T_DBL_ARR : T_INT_ARR) : base;

        /* ініціалізатор обробляємо ДО оголошення: int x = x; — x справа ще зовнішній */
        if (init) {
            if (init->kind == N_INITLIST) {
                if (!is_arr) error(v->line, "initializer list for non-array '%s'", v->str, NULL);
                for (int k = 0; k < init->nkids; k++) { expr(init->kids[k]); need_scalar(init->kids[k], "initializer"); }
                if (is_arr && v->ival == 0) v->ival = init->nkids;
                if (is_arr && init->nkids > v->ival) error(v->line, "too many initializers for '%s'", v->str, NULL);
            } else {
                if (is_arr) error(v->line, "array '%s' must be initialized with { ... }", v->str, NULL);
                expr(init);
                need_scalar(init, "initializer");
            }
        }
        if (is_arr && v->ival <= 0) error(v->line, "array '%s' must have positive size", v->str, NULL);
        v->type = t;
        v->sym = declare(v->str, t, v->line, global);
    }
}

/* ---------- оператори ---------- */

static void stmt(Node *n);

static void block_body(Node *b) {
    for (int i = 0; i < b->nkids; i++) {
        Node *s = b->kids[i];
        if (s->kind == N_VARDECL) vardecl(s, 0);
        else stmt(s);
    }
}

static void cond(Node *c) {
    if (c->kind == N_EMPTY) return;
    expr(c);
    need_scalar(c, "condition");
}

static void stmt(Node *n) {
    switch (n->kind) {
    case N_BLOCK: push_scope(); block_body(n); pop_scope(); break;
    case N_VARDECL: vardecl(n, 0); break;
    case N_EXPRSTMT: expr(n->kids[0]); break;
    case N_EMPTY: break;
    case N_IF:
        cond(n->kids[0]);
        stmt(n->kids[1]);
        if (n->nkids > 2) stmt(n->kids[2]);
        break;
    case N_WHILE:
        cond(n->kids[0]);
        loop_depth++; stmt(n->kids[1]); loop_depth--;
        break;
    case N_DOWHILE:
        loop_depth++; stmt(n->kids[0]); loop_depth--;
        cond(n->kids[1]);
        break;
    case N_FOR:
        push_scope();   /* for (int i = ...) — i видно тільки всередині циклу */
        if (n->kids[0]->kind == N_VARDECL) vardecl(n->kids[0], 0);
        else if (n->kids[0]->kind == N_EXPRSTMT) expr(n->kids[0]->kids[0]);
        cond(n->kids[1]);
        if (n->kids[2]->kind != N_EMPTY) expr(n->kids[2]);
        loop_depth++; stmt(n->kids[3]); loop_depth--;
        pop_scope();
        break;
    case N_RETURN:
        if (n->nkids) {
            expr(n->kids[0]);
            if (cur_func->ret == T_VOID) error(n->line, "void function '%s' returns a value", cur_func->name, NULL);
            else need_scalar(n->kids[0], "return value");
        } else if (cur_func->ret != T_VOID)
            error(n->line, "function '%s' must return a value", cur_func->name, NULL);
        break;
    case N_BREAK:
        if (!loop_depth) error(n->line, "break outside of loop%s", "", NULL);
        break;
    case N_CONTINUE:
        if (!loop_depth) error(n->line, "continue outside of loop%s", "", NULL);
        break;
    default:
        error(n->line, "unexpected node %s", kind_name(n->kind), NULL);
    }
}

/* ---------- верхній рівень ---------- */

/* реєструємо функцію (з визначення або прототипу) */
static void register_func(Node *n) {
    Node *params = n->kids[0];
    FuncInfo *f = find_func(n->str);
    if (f && f->builtin) { error(n->line, "'%s' is a built-in function", n->str, NULL); return; }
    if (f) {
        if (f->nparams != params->nkids)
            error(n->line, "conflicting declaration of '%s'", n->str, NULL);
        if (n->kind == N_FUNC) {
            if (f->def) error(n->line, "redefinition of function '%s'", n->str, NULL);
            else f->def = n;
        }
        n->fn = f;
        return;
    }
    if (params->nkids > 32) { error(n->line, "too many parameters in '%s'", n->str, NULL); return; }
    f = xmalloc(sizeof *f);
    f->name = n->str;
    f->ret = type_from_name(n->tname);
    f->nparams = params->nkids;
    for (int i = 0; i < params->nkids; i++) {
        Node *p = params->kids[i];
        Type t = type_from_name(p->tname);
        if (p->ival) t = (t == T_DOUBLE) ? T_DBL_ARR : T_INT_ARR;
        f->ptypes[i] = t;
    }
    f->def = n->kind == N_FUNC ? n : NULL;
    f->pyname = make_pyname(n->str);
    f->next = functions;
    functions = f;
    n->fn = f;
}

static void func_body(Node *n) {
    FuncInfo *f = n->fn;
    if (!f || f->def != n) return;
    cur_func = f;
    nused = nused_globals;   /* локальні імена починаємо заново */
    push_scope();
    Node *params = n->kids[0];
    for (int i = 0; i < params->nkids; i++) {
        Node *p = params->kids[i];
        if (type_from_name(p->tname) == T_VOID) { error(p->line, "parameter '%s' declared void", p->str, NULL); continue; }
        p->type = f->ptypes[i];
        p->sym = declare(p->str, f->ptypes[i], p->line, 0);
        f->params[i] = p->sym;
    }
    block_body(n->kids[1]);   /* тіло функції — та сама область, що й параметри */
    pop_scope();
    cur_func = NULL;
}

int sema_check(Node *prog) {
    errors = 0;
    add_builtin("printf", B_PRINTF, T_INT, 0, T_NONE, T_NONE)->variadic = 1;
    add_builtin("putchar", B_PUTCHAR, T_INT, 1, T_INT, T_NONE);
    add_builtin("abs", B_ABS, T_INT, 1, T_INT, T_NONE);
    add_builtin("fabs", B_FABS, T_DOUBLE, 1, T_DOUBLE, T_NONE);
    add_builtin("sqrt", B_SQRT, T_DOUBLE, 1, T_DOUBLE, T_NONE);
    add_builtin("pow", B_POW, T_DOUBLE, 2, T_DOUBLE, T_DOUBLE);

    /* 1-й прохід: усі функції (щоб можна було викликати до визначення) */
    for (int i = 0; i < prog->nkids; i++)
        if (prog->kids[i]->kind == N_FUNC || prog->kids[i]->kind == N_PROTO)
            register_func(prog->kids[i]);
    nused_globals = nused;   /* імена функцій локальні змінні теж не мають займати */

    /* 2-й прохід: глобальні змінні та тіла функцій у порядку тексту */
    push_scope();
    for (int i = 0; i < prog->nkids; i++) {
        Node *n = prog->kids[i];
        if (n->kind == N_VARDECL) {
            vardecl(n, 1);
            nused_globals = nused;
        }
    }
    for (int i = 0; i < prog->nkids; i++)
        if (prog->kids[i]->kind == N_FUNC) func_body(prog->kids[i]);
    pop_scope();

    for (FuncInfo *f = functions; f; f = f->next)
        if (!f->builtin && !f->def)
            error(1, "function '%s' declared but never defined", f->name, NULL);

    return errors;
}
