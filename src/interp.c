/*
 * Інтерпретатор AST (обчислення програми "на льоту").
 * Обходить дерево після семантичного аналізу: кожна змінна вже має slot,
 * тому фрейм функції — це просто масив значень.
 */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <setjmp.h>
#include "interp.h"
#include "sema.h"

typedef struct Value Value;

typedef struct Array {
    int len;
    Value *data;
} Array;

struct Value {
    Type k;           /* T_INT, T_DOUBLE, T_INT_ARR, T_DBL_ARR, T_STR, T_VOID */
    long long i;
    double d;
    Array *arr;
    const char *s;
};

typedef struct Frame {
    Value *slots;
    Value ret;
} Frame;

typedef enum { EX_NORMAL, EX_BREAK, EX_CONTINUE, EX_RETURN } Exec;

static Value *globals;
static jmp_buf on_error;
static int call_depth = 0;

static void rt_error(int line, const char *msg) {
    fflush(stdout);
    fprintf(stderr, "line %d: runtime error: %s\n", line, msg);
    longjmp(on_error, 1);
}

/* ---------- значення ---------- */

static Value v_int(long long x) { Value v = {0}; v.k = T_INT; v.i = x; return v; }
static Value v_dbl(double x) { Value v = {0}; v.k = T_DOUBLE; v.d = x; return v; }

static double as_dbl(Value v) { return v.k == T_DOUBLE ? v.d : (double)v.i; }
static long long as_int(Value v) { return v.k == T_DOUBLE ? (long long)v.d : v.i; }
static int truthy(Value v) { return v.k == T_DOUBLE ? v.d != 0.0 : v.i != 0; }

/* неявне перетворення при присвоєнні / передачі параметра */
static Value convert(Value v, Type t) {
    if (t == T_INT && v.k == T_DOUBLE) return v_int((long long)v.d);
    if (t == T_DOUBLE && v.k == T_INT) return v_dbl((double)v.i);
    return v;
}

static Array *new_array(int len, Type elem) {
    Array *a = xmalloc(sizeof *a);
    a->len = len;
    a->data = xmalloc(sizeof(Value) * (len > 0 ? len : 1));
    for (int i = 0; i < len; i++) a->data[i] = elem == T_DOUBLE ? v_dbl(0) : v_int(0);
    return a;
}

/* ---------- вирази ---------- */

static Value eval(Node *n, Frame *f);
static Value call(Node *n, Frame *f);

static Value *slot_of(Symbol *s, Frame *f) {
    return s->is_global ? &globals[s->slot] : &f->slots[s->slot];
}

/* адреса, куди можна записати: змінна або елемент масиву */
static Value *lvalue(Node *n, Frame *f) {
    if (n->kind == N_ID) return slot_of(n->sym, f);
    /* N_INDEX */
    Value *arr = slot_of(n->kids[0]->sym, f);
    long long i = as_int(eval(n->kids[1], f));
    if (i < 0 || i >= arr->arr->len) {
        char buf[128];
        snprintf(buf, sizeof buf, "index %lld out of bounds for '%s[%d]'", i, n->kids[0]->str, arr->arr->len);
        rt_error(n->line, buf);
    }
    return &arr->arr->data[i];
}

static Value arith(Node *n, const char *op, Value a, Value b) {
    if (a.k == T_DOUBLE || b.k == T_DOUBLE) {
        double x = as_dbl(a), y = as_dbl(b);
        switch (op[0]) {
        case '+': return v_dbl(x + y);
        case '-': return v_dbl(x - y);
        case '*': return v_dbl(x * y);
        case '/': return v_dbl(x / y);   /* як у C: 1.0/0 = inf */
        }
    } else {
        long long x = a.i, y = b.i;
        switch (op[0]) {
        case '+': return v_int(x + y);
        case '-': return v_int(x - y);
        case '*': return v_int(x * y);
        case '/': if (!y) rt_error(n->line, "integer division by zero"); return v_int(x / y);
        case '%': if (!y) rt_error(n->line, "integer division by zero"); return v_int(x % y);
        }
    }
    rt_error(n->line, "bad arithmetic operator");
    return v_int(0);
}

static Value compare(const char *op, Value a, Value b) {
    double x = as_dbl(a), y = as_dbl(b);
    int r = 0;
    if (a.k == T_INT && b.k == T_INT) {   /* щоб великі long long порівнювались точно */
        long long p = a.i, q = b.i;
        if (!strcmp(op, "<")) r = p < q; else if (!strcmp(op, ">")) r = p > q;
        else if (!strcmp(op, "<=")) r = p <= q; else if (!strcmp(op, ">=")) r = p >= q;
        else if (!strcmp(op, "==")) r = p == q; else r = p != q;
        return v_int(r);
    }
    if (!strcmp(op, "<")) r = x < y; else if (!strcmp(op, ">")) r = x > y;
    else if (!strcmp(op, "<=")) r = x <= y; else if (!strcmp(op, ">=")) r = x >= y;
    else if (!strcmp(op, "==")) r = x == y; else r = x != y;
    return v_int(r);
}

static Value eval(Node *n, Frame *f) {
    switch (n->kind) {
    case N_INT: case N_CHAR: return v_int(n->ival);
    case N_DOUBLE: return v_dbl(n->dval);
    case N_STRING: { Value v = {0}; v.k = T_STR; v.s = n->str; return v; }
    case N_ID: return *slot_of(n->sym, f);
    case N_INDEX: return *lvalue(n, f);
    case N_CALL: return call(n, f);

    case N_BINOP: {
        const char *op = n->str;
        /* && і || — "ледачі": правий операнд рахуємо тільки якщо треба */
        if (!strcmp(op, "&&")) return v_int(truthy(eval(n->kids[0], f)) && truthy(eval(n->kids[1], f)));
        if (!strcmp(op, "||")) return v_int(truthy(eval(n->kids[0], f)) || truthy(eval(n->kids[1], f)));
        Value a = eval(n->kids[0], f), b = eval(n->kids[1], f);
        if (strchr("+-*/%", op[0]) && op[1] == '\0') return arith(n, op, a, b);
        return compare(op, a, b);
    }
    case N_UNOP: {
        Value a = eval(n->kids[0], f);
        if (n->str[0] == '!') return v_int(!truthy(a));
        if (n->str[0] == '-') return a.k == T_DOUBLE ? v_dbl(-a.d) : v_int(-a.i);
        return a;
    }
    case N_PREINC: case N_POSTINC: {
        Value *p = lvalue(n->kids[0], f);
        Value old = *p;
        int delta = n->str[0] == '+' ? 1 : -1;
        if (p->k == T_DOUBLE) p->d += delta; else p->i += delta;
        return n->kind == N_PREINC ? *p : old;
    }
    case N_ASSIGN: {
        Value r = eval(n->kids[1], f);
        Value *p = lvalue(n->kids[0], f);
        if (n->str[0] != '=') {   /* +=, -=, ... */
            char op[2] = { n->str[0], 0 };
            r = arith(n, op, *p, r);
        }
        *p = convert(r, n->kids[0]->type);
        return *p;
    }
    case N_TERNARY:
        return convert(truthy(eval(n->kids[0], f)) ? eval(n->kids[1], f) : eval(n->kids[2], f), n->type);
    case N_CAST:
        return convert(eval(n->kids[0], f), n->type);
    default:
        rt_error(n->line, "cannot evaluate node");
    }
    return v_int(0);
}

/* ---------- printf ---------- */

static char *unescape(const char *s) {
    char *r = xmalloc(strlen(s) + 1), *o = r;
    for (; *s; s++) {
        if (*s != '\\') { *o++ = *s; continue; }
        s++;
        switch (*s) {
        case 'n': *o++ = '\n'; break;
        case 't': *o++ = '\t'; break;
        case 'r': *o++ = '\r'; break;
        case '0': *o++ = '\0'; break;
        case 'a': *o++ = '\a'; break;
        case '\0': s--; break;
        default: *o++ = *s;
        }
    }
    *o = 0;
    return r;
}

static int do_printf(Node *n, Frame *f) {
    char *fmt = unescape(n->kids[0]->str);
    int argi = 1, total = 0;
    char spec[64], out[512];

    /* спочатку рахуємо всі аргументи, потім друкуємо (як справжній printf) */
    if (n->nkids > 64) rt_error(n->line, "printf: too many arguments");
    Value args[64];
    for (int i = 1; i < n->nkids; i++) args[i] = eval(n->kids[i], f);

    for (char *p = fmt; *p; p++) {
        if (*p != '%') { putchar(*p); total++; continue; }
        p++;
        if (*p == '%') { putchar('%'); total++; continue; }

        /* збираємо специфікатор: прапорці, ширина, точність */
        int k = 0;
        spec[k++] = '%';
        while (*p && strchr("-+ #0", *p) && k < 20) spec[k++] = *p++;
        while (*p >= '0' && *p <= '9' && k < 30) spec[k++] = *p++;
        if (*p == '.') { spec[k++] = *p++; while (*p >= '0' && *p <= '9' && k < 40) spec[k++] = *p++; }
        while (*p && strchr("hlLqjzt", *p)) p++;   /* модифікатори довжини ігноруємо */
        char conv = *p;
        if (!conv) break;

        if (argi >= n->nkids) rt_error(n->line, "printf: not enough arguments for format");
        Value v = args[argi++];

        if (strchr("di", conv)) { strcpy(spec + k, "lld"); snprintf(out, sizeof out, spec, as_int(v)); }
        else if (strchr("uxXo", conv)) { spec[k++] = 'l'; spec[k++] = 'l'; spec[k++] = conv; spec[k] = 0;
                                         snprintf(out, sizeof out, spec, as_int(v)); }
        else if (conv == 'c') { spec[k++] = 'c'; spec[k] = 0; snprintf(out, sizeof out, spec, (int)as_int(v)); }
        else if (strchr("fFeEgG", conv)) { spec[k++] = conv; spec[k] = 0; snprintf(out, sizeof out, spec, as_dbl(v)); }
        else if (conv == 's') {
            if (v.k != T_STR) rt_error(n->line, "printf: %s expects a string");
            char *s = unescape(v.s);
            spec[k++] = 's'; spec[k] = 0;
            snprintf(out, sizeof out, spec, s);
            free(s);
        } else {
            char buf[64];
            snprintf(buf, sizeof buf, "printf: unsupported conversion %%%c", conv);
            rt_error(n->line, buf);
        }
        fputs(out, stdout);
        total += strlen(out);
    }
    free(fmt);
    return total;
}

/* ---------- оператори ---------- */

static Exec exec(Node *n, Frame *f);

static void declare_vars(Node *d, Frame *f) {
    for (int i = 0; i < d->nkids; i++) {
        Node *v = d->kids[i];
        if (!v->sym) continue;
        Value *slot = slot_of(v->sym, f);
        Node *init = v->nkids ? v->kids[0] : NULL;
        if (v->type == T_INT_ARR || v->type == T_DBL_ARR) {
            Type elem = v->type == T_DBL_ARR ? T_DOUBLE : T_INT;
            Value a = {0};
            a.k = v->type;
            a.arr = new_array((int)v->ival, elem);
            if (init)
                for (int k = 0; k < init->nkids; k++)
                    a.arr->data[k] = convert(eval(init->kids[k], f), elem);
            *slot = a;
        } else {
            /* неініціалізована змінна в C містить сміття — у нас просто 0 */
            *slot = init ? convert(eval(init, f), v->type) : convert(v_int(0), v->type);
        }
    }
}

static Exec exec_block(Node *b, Frame *f) {
    for (int i = 0; i < b->nkids; i++) {
        Exec r = exec(b->kids[i], f);
        if (r != EX_NORMAL) return r;
    }
    return EX_NORMAL;
}

static Exec exec(Node *n, Frame *f) {
    switch (n->kind) {
    case N_BLOCK: return exec_block(n, f);
    case N_VARDECL: declare_vars(n, f); return EX_NORMAL;
    case N_EXPRSTMT: eval(n->kids[0], f); return EX_NORMAL;
    case N_EMPTY: return EX_NORMAL;

    case N_IF:
        if (truthy(eval(n->kids[0], f))) return exec(n->kids[1], f);
        if (n->nkids > 2) return exec(n->kids[2], f);
        return EX_NORMAL;

    case N_WHILE:
        while (truthy(eval(n->kids[0], f))) {
            Exec r = exec(n->kids[1], f);
            if (r == EX_BREAK) break;
            if (r == EX_RETURN) return r;
        }
        return EX_NORMAL;

    case N_DOWHILE:
        do {
            Exec r = exec(n->kids[0], f);
            if (r == EX_BREAK) break;
            if (r == EX_RETURN) return r;
        } while (truthy(eval(n->kids[1], f)));
        return EX_NORMAL;

    case N_FOR: {
        Node *init = n->kids[0], *c = n->kids[1], *step = n->kids[2], *body = n->kids[3];
        if (init->kind != N_EMPTY) exec(init, f);
        while (c->kind == N_EMPTY || truthy(eval(c, f))) {
            Exec r = exec(body, f);
            if (r == EX_BREAK) break;
            if (r == EX_RETURN) return r;
            if (step->kind != N_EMPTY) eval(step, f);   /* continue теж потрапляє сюди */
        }
        return EX_NORMAL;
    }
    case N_RETURN:
        f->ret = n->nkids ? eval(n->kids[0], f) : v_int(0);
        return EX_RETURN;
    case N_BREAK: return EX_BREAK;
    case N_CONTINUE: return EX_CONTINUE;
    default:
        rt_error(n->line, "cannot execute node");
    }
    return EX_NORMAL;
}

/* ---------- виклики ---------- */

static Value call(Node *n, Frame *f) {
    FuncInfo *fn = n->fn;
    switch (fn->builtin) {
    case B_PRINTF: return v_int(do_printf(n, f));
    case B_PUTCHAR: { long long c = as_int(eval(n->kids[0], f)); putchar((int)c); return v_int(c); }
    case B_ABS: { long long x = as_int(eval(n->kids[0], f)); return v_int(x < 0 ? -x : x); }
    case B_FABS: return v_dbl(fabs(as_dbl(eval(n->kids[0], f))));
    case B_SQRT: return v_dbl(sqrt(as_dbl(eval(n->kids[0], f))));
    case B_POW: { double a = as_dbl(eval(n->kids[0], f)); return v_dbl(pow(a, as_dbl(eval(n->kids[1], f)))); }
    default: break;
    }

    if (++call_depth > 20000) rt_error(n->line, "stack overflow (too deep recursion)");

    Frame nf;
    nf.slots = xmalloc(sizeof(Value) * (fn->nslots + 1));
    nf.ret = v_int(0);
    /* аргументи рахуємо в старому фреймі, кладемо в новий */
    for (int i = 0; i < fn->nparams; i++) {
        Value a = eval(n->kids[i], f);
        if (fn->params[i]) nf.slots[fn->params[i]->slot] = convert(a, fn->ptypes[i]);
    }
    exec_block(fn->def->kids[1], &nf);
    free(nf.slots);
    call_depth--;

    if (fn->ret == T_VOID) { Value v = {0}; v.k = T_VOID; return v; }
    return convert(nf.ret, fn->ret);
}

int interp_run(Node *prog) {
    FuncInfo *main_fn = find_func("main");
    if (!main_fn || !main_fn->def) {
        fprintf(stderr, "error: no main() function\n");
        return -1;
    }
    globals = xmalloc(sizeof(Value) * (nglobals + 1));
    if (setjmp(on_error)) return -1;

    Frame top = { NULL, {0} };
    for (int i = 0; i < prog->nkids; i++)
        if (prog->kids[i]->kind == N_VARDECL) declare_vars(prog->kids[i], &top);

    Node fake = {0};
    fake.kind = N_CALL;
    fake.str = "main";
    fake.fn = main_fn;
    fake.line = main_fn->def->line;
    Value r = call(&fake, &top);
    fflush(stdout);
    return main_fn->ret == T_VOID ? 0 : (int)as_int(r);
}
