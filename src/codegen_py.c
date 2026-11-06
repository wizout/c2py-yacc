/*
 * Генерація коду: AST мови C  ->  програма на Python 3.
 *
 * Основні проблеми трансляції і як вони вирішені:
 *   - цілочисельне ділення в C відкидає дробову частину до нуля, а // у Python
 *     округлює вниз  -> хелпери _idiv/_imod;
 *   - у Python немає ++, --, а присвоєння не є виразом -> оператор := ("морж"),
 *     для елементів масиву — хелпери _set/_post;
 *   - у Python немає for(;;) -> range(), якщо цикл "простий", інакше while;
 *   - у C порівняння дає int 0/1 -> у "значеннєвому" контексті int(a < b);
 *   - блоки в Python не створюють область видимості -> імена змінних
 *     заздалегідь зроблені унікальними семантичним аналізом (x, x_2, ...).
 */
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "codegen.h"
#include "sema.h"

/* ---------- простий string builder ---------- */

typedef struct { char *buf; size_t len, cap; } SB;

static void sb_printf(SB *b, const char *fmt, ...) {
    va_list ap;
    for (;;) {
        size_t room = b->cap - b->len;
        va_start(ap, fmt);
        int n = vsnprintf(b->buf ? b->buf + b->len : NULL, b->buf ? room : 0, fmt, ap);
        va_end(ap);
        if (b->buf && (size_t)n < room) { b->len += n; return; }
        b->cap = (b->cap + n + 1) * 2;
        b->buf = realloc(b->buf, b->cap);
        if (!b->len) b->buf[0] = 0;
    }
}

static void sb_free(SB *b) { free(b->buf); b->buf = NULL; b->len = b->cap = 0; }
static const char *sb_str(SB *b) { return b->buf ? b->buf : ""; }

/* ---------- стан генератора ---------- */

static SB body;              /* сюди пишемо все, крім заголовка */
static int ind = 0;          /* поточний відступ */
static int uid = 0;          /* для унікальних імен прапорців */
static FuncInfo *cur_fn;

static int need_idiv, need_imod, need_set, need_post, need_printf, need_putchar, need_math;

static void line(const char *fmt, ...) {
    va_list ap;
    char tmp[4096];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    sb_printf(&body, "%*s%s\n", ind * 4, "", tmp);
}

/* ---------- вирази ---------- */

/* пріоритети операцій у Python (більше = зв'язує сильніше) */
enum { P_LOWEST = 0, P_TERN = 1, P_OR = 2, P_AND = 3, P_NOT = 4, P_CMP = 5,
       P_ADD = 8, P_MUL = 9, P_UNARY = 10, P_ATOM = 12 };

/* контекст: чи потрібне значення (int 0/1), чи достатньо істинності */
enum { VAL, COND };

static void gx(SB *b, Node *n, int prec, int ctx);

static void open_p(SB *b, int my, int need) { if (my < need) sb_printf(b, "("); }
static void close_p(SB *b, int my, int need) { if (my < need) sb_printf(b, ")"); }

/* вираз, приведений до типу t (int(x), якщо double присвоюється в int) */
static void gx_as(SB *b, Node *n, Type t, int prec) {
    if (t == T_INT && n->type == T_DOUBLE) {
        sb_printf(b, "int(");
        gx(b, n, P_LOWEST, VAL);
        sb_printf(b, ")");
    } else gx(b, n, prec, VAL);
}

static const char *py_cmp(const char *op) { return op; }   /* < > <= >= == != однакові */

/* C-рядок формату printf -> рядок для оператора % у Python (прибираємо l, h ...) */
static char *py_format(const char *s, int has_args) {
    char *r = xmalloc(strlen(s) * 2 + 1), *o = r;
    while (*s) {
        if (*s == '\\' && s[1]) { *o++ = *s++; *o++ = *s++; continue; }
        if (*s != '%') { *o++ = *s++; continue; }
        if (s[1] == '%') {
            if (has_args) { *o++ = '%'; *o++ = '%'; } else *o++ = '%';
            s += 2;
            continue;
        }
        *o++ = *s++;
        while (*s && strchr("-+ #0123456789.", *s)) *o++ = *s++;
        while (*s && strchr("hlLqjzt", *s)) s++;
        if (*s) *o++ = *s++;
    }
    *o = 0;
    return r;
}

static void gx_args(SB *b, Node *call, int from) {
    for (int i = from; i < call->nkids; i++) {
        if (i > from) sb_printf(b, ", ");
        gx(b, call->kids[i], P_LOWEST, VAL);
    }
}

static void gx_call(SB *b, Node *n) {
    FuncInfo *f = n->fn;
    switch (f->builtin) {
    case B_PRINTF: {
        char *fmt = py_format(n->kids[0]->str, 1);
        need_printf = 1;
        sb_printf(b, "_printf(\"%s\"", fmt);
        if (n->nkids > 1) { sb_printf(b, ", "); gx_args(b, n, 1); }
        sb_printf(b, ")");
        free(fmt);
        return;
    }
    case B_PUTCHAR: need_putchar = 1; sb_printf(b, "_putchar("); gx_args(b, n, 0); sb_printf(b, ")"); return;
    case B_ABS: sb_printf(b, "abs("); gx_args(b, n, 0); sb_printf(b, ")"); return;
    case B_FABS: need_math = 1; sb_printf(b, "math.fabs("); gx_args(b, n, 0); sb_printf(b, ")"); return;
    case B_SQRT: need_math = 1; sb_printf(b, "math.sqrt("); gx_args(b, n, 0); sb_printf(b, ")"); return;
    case B_POW: need_math = 1; sb_printf(b, "math.pow("); gx_args(b, n, 0); sb_printf(b, ")"); return;
    default: break;
    }
    sb_printf(b, "%s(", f->pyname);
    for (int i = 0; i < n->nkids; i++) {
        if (i) sb_printf(b, ", ");
        gx_as(b, n->kids[i], f->ptypes[i], P_LOWEST);
    }
    sb_printf(b, ")");
}

/* a op b для арифметики з урахуванням цілого ділення */
static void gx_arith(SB *b, const char *op, Node *l, Node *r, Type t, int prec) {
    if (t == T_INT && (op[0] == '/' || op[0] == '%')) {
        if (op[0] == '/') { need_idiv = 1; sb_printf(b, "_idiv("); }
        else { need_imod = 1; sb_printf(b, "_imod("); }
        gx(b, l, P_LOWEST, VAL);
        sb_printf(b, ", ");
        gx(b, r, P_LOWEST, VAL);
        sb_printf(b, ")");
        return;
    }
    int my = (op[0] == '+' || op[0] == '-') ? P_ADD : P_MUL;
    open_p(b, my, prec);
    gx(b, l, my, VAL);
    sb_printf(b, " %c ", op[0]);
    gx(b, r, my + 1, VAL);
    close_p(b, my, prec);
}

/* значення "lhs op rhs" для складеного присвоєння, вже приведене до типу lhs */
static void gx_compound_value(SB *b, Node *lhs, const char *op, Node *rhs) {
    Type res = (lhs->type == T_DOUBLE || rhs->type == T_DOUBLE) ? T_DOUBLE : T_INT;
    int cast = lhs->type == T_INT && res == T_DOUBLE;
    if (cast) sb_printf(b, "int(");
    gx_arith(b, op, lhs, rhs, res, cast ? P_LOWEST : P_LOWEST);
    if (cast) sb_printf(b, ")");
}

static void gx_double(SB *b, double d) {
    char buf[64];
    snprintf(buf, sizeof buf, "%.15g", d);
    if (strtod(buf, NULL) != d) snprintf(buf, sizeof buf, "%.17g", d);
    if (!strpbrk(buf, ".eni")) strcat(buf, ".0");
    sb_printf(b, "%s", buf);
}

static void gx(SB *b, Node *n, int prec, int ctx) {
    switch (n->kind) {
    case N_INT:
        if (ctx == COND && prec <= P_TERN) sb_printf(b, n->ival ? "True" : "False");
        else sb_printf(b, "%lld", n->ival);
        return;
    case N_DOUBLE: gx_double(b, n->dval); return;
    case N_CHAR: sb_printf(b, "ord(%s)", n->str); return;
    case N_STRING: sb_printf(b, "\"%s\"", n->str); return;
    case N_ID: sb_printf(b, "%s", n->sym->pyname); return;
    case N_INDEX:
        sb_printf(b, "%s[", n->kids[0]->sym->pyname);
        gx(b, n->kids[1], P_LOWEST, VAL);
        sb_printf(b, "]");
        return;
    case N_CALL: gx_call(b, n); return;
    case N_CAST:
        sb_printf(b, n->type == T_DOUBLE ? "float(" : "int(");
        gx(b, n->kids[0], P_LOWEST, VAL);
        sb_printf(b, ")");
        return;

    case N_BINOP: {
        const char *op = n->str;
        Node *l = n->kids[0], *r = n->kids[1];
        int is_logic = !strcmp(op, "&&") || !strcmp(op, "||");
        int is_cmp = strchr("<>=!", op[0]) && !is_logic;

        if (is_logic) {
            int my = op[0] == '&' ? P_AND : P_OR;
            if (ctx == VAL) {   /* 1 if a and b else 0 */
                open_p(b, P_TERN, prec);
                sb_printf(b, "1 if ");
                gx(b, n, P_OR, COND);
                sb_printf(b, " else 0");
                close_p(b, P_TERN, prec);
                return;
            }
            open_p(b, my, prec);
            gx(b, l, my, COND);
            sb_printf(b, my == P_AND ? " and " : " or ");
            gx(b, r, my + 1, COND);
            close_p(b, my, prec);
            return;
        }
        if (is_cmp) {
            if (ctx == VAL) sb_printf(b, "int(");
            int need = ctx == VAL ? P_LOWEST : prec;
            open_p(b, P_CMP, need);
            gx(b, l, P_CMP + 1, VAL);   /* +1: щоб a < b < c не стало ланцюжком Python */
            sb_printf(b, " %s ", py_cmp(op));
            gx(b, r, P_CMP + 1, VAL);
            close_p(b, P_CMP, need);
            if (ctx == VAL) sb_printf(b, ")");
            return;
        }
        gx_arith(b, op, l, r, n->type, prec);
        return;
    }

    case N_UNOP:
        if (n->str[0] == '!') {
            if (ctx == VAL) { sb_printf(b, "int(not "); gx(b, n->kids[0], P_NOT, COND); sb_printf(b, ")"); return; }
            open_p(b, P_NOT, prec);
            sb_printf(b, "not ");
            gx(b, n->kids[0], P_NOT, COND);
            close_p(b, P_NOT, prec);
            return;
        }
        if (n->str[0] == '+') { gx(b, n->kids[0], prec, ctx); return; }
        open_p(b, P_UNARY, prec);
        sb_printf(b, "-");
        gx(b, n->kids[0], P_UNARY, VAL);
        close_p(b, P_UNARY, prec);
        return;

    case N_TERNARY:
        open_p(b, P_TERN, prec);
        gx_as(b, n->kids[1], n->type, P_OR);
        sb_printf(b, " if ");
        gx(b, n->kids[0], P_OR, COND);
        sb_printf(b, " else ");
        gx_as(b, n->kids[2], n->type, P_TERN);
        close_p(b, P_TERN, prec);
        return;

    /* присвоєння всередині виразу: x := ..., або _set(a, i, ...) для масиву */
    case N_ASSIGN: case N_PREINC: case N_POSTINC: {
        Node *lhs = n->kids[0];
        int post = n->kind == N_POSTINC;
        if (lhs->kind == N_INDEX) {
            const char *arr = lhs->kids[0]->sym->pyname;
            if (post) {
                need_post = 1;
                sb_printf(b, "_post(%s, ", arr);
                gx(b, lhs->kids[1], P_LOWEST, VAL);
                sb_printf(b, ", %d)", n->str[0] == '+' ? 1 : -1);
                return;
            }
            need_set = 1;
            sb_printf(b, "_set(%s, ", arr);
            gx(b, lhs->kids[1], P_LOWEST, VAL);
            sb_printf(b, ", ");
        } else {
            if (post) sb_printf(b, "(");
            sb_printf(b, "(%s := ", lhs->sym->pyname);
        }

        if (n->kind == N_ASSIGN && n->str[0] == '=') gx_as(b, n->kids[1], lhs->type, P_LOWEST);
        else if (n->kind == N_ASSIGN) {
            char op[2] = { n->str[0], 0 };
            gx_compound_value(b, lhs, op, n->kids[1]);
        } else {
            gx(b, lhs, P_ADD, VAL);
            sb_printf(b, " %c 1", n->str[0]);
        }
        sb_printf(b, ")");
        if (post) sb_printf(b, " %c 1)", n->str[0] == '+' ? '-' : '+');   /* старе значення */
        return;
    }
    default:
        sb_printf(b, "None  # ???");
    }
}

/* вираз у рядок (для генерації рядків коду) */
static char *xs(Node *n, int ctx) {
    SB b = {0};
    gx(&b, n, P_LOWEST, ctx);
    char *r = xstrdup(sb_str(&b));
    sb_free(&b);
    return r;
}

static char *xs_as(Node *n, Type t) {
    SB b = {0};
    gx_as(&b, n, t, P_LOWEST);
    char *r = xstrdup(sb_str(&b));
    sb_free(&b);
    return r;
}

/* not (cond) — для "if not cond: break" */
static char *xs_not(Node *n) {
    SB b = {0};
    sb_printf(&b, "not ");
    gx(&b, n, P_NOT, COND);
    char *r = xstrdup(sb_str(&b));
    sb_free(&b);
    return r;
}

/* ---------- оператори ---------- */

static void gs(Node *n);

/* тіло if/while/def: якщо нічого не згенерувалось — pass */
static void suite(Node *n) {
    ind++;
    size_t before = body.len;
    gs(n);
    if (body.len == before) line("pass");
    ind--;
}

static void zero_init_line(Node *v) {
    const char *name = v->sym->pyname;
    Node *init = v->nkids ? v->kids[0] : NULL;
    int dbl = v->type == T_DOUBLE || v->type == T_DBL_ARR;
    const char *zero = dbl ? "0.0" : "0";

    if (v->type == T_INT_ARR || v->type == T_DBL_ARR) {
        int all_zero = init != NULL;
        for (int i = 0; init && i < init->nkids; i++)
            if (init->kids[i]->kind != N_INT || init->kids[i]->ival != 0) all_zero = 0;
        if (!init || all_zero) { line("%s = [%s] * %lld", name, zero, v->ival); return; }
        SB b = {0};
        sb_printf(&b, "[");
        for (int i = 0; i < init->nkids; i++) {
            if (i) sb_printf(&b, ", ");
            if (dbl && init->kids[i]->kind == N_INT) gx_double(&b, (double)init->kids[i]->ival);
            else gx_as(&b, init->kids[i], dbl ? T_DOUBLE : T_INT, P_LOWEST);
        }
        sb_printf(&b, "]");
        if (init->nkids < v->ival) sb_printf(&b, " + [%s] * %lld", zero, v->ival - init->nkids);
        line("%s = %s", name, sb_str(&b));
        sb_free(&b);
        return;
    }
    if (!init) { line("%s = %s", name, zero); return; }
    if (dbl && init->kind == N_INT) {   /* double x = 5;  ->  x = 5.0 */
        SB b = {0};
        gx_double(&b, (double)init->ival);
        line("%s = %s", name, sb_str(&b));
        sb_free(&b);
        return;
    }
    char *e = xs_as(init, v->type);
    line("%s = %s", name, e);
    free(e);
}

static void gen_vardecl(Node *d) {
    for (int i = 0; i < d->nkids; i++)
        if (d->kids[i]->sym) zero_init_line(d->kids[i]);
}

/* ціль присвоєння як рядок: x або a[i] */
static char *target(Node *lhs) {
    if (lhs->kind == N_ID) return xstrdup(lhs->sym->pyname);
    return xs(lhs, VAL);
}

static void gen_printf_stmt(Node *n) {
    int has_args = n->nkids > 1;
    char *fmt = py_format(n->kids[0]->str, has_args);
    size_t L = strlen(fmt);
    /* printf("...\n") -> print("...") */
    int nl = L >= 2 && fmt[L - 2] == '\\' && fmt[L - 1] == 'n' && !(L >= 3 && fmt[L - 3] == '\\');
    if (nl) fmt[L - 2] = 0;
    const char *end = nl ? "" : ", end=\"\"";

    if (!has_args) {
        if (nl && !fmt[0]) line("print()");
        else line("print(\"%s\"%s)", fmt, end);
        free(fmt);
        return;
    }

    SB b = {0};
    sb_printf(&b, "print(\"%s\" %% ", fmt);
    if (n->nkids == 2) gx(&b, n->kids[1], P_UNARY, VAL);
    else { sb_printf(&b, "("); gx_args(&b, n, 1); sb_printf(&b, ")"); }
    sb_printf(&b, "%s)", end);
    line("%s", sb_str(&b));
    sb_free(&b);
    free(fmt);
}

/* вираз як оператор: x = ..., x += 1, f(...) */
static void gen_exprstmt(Node *e) {
    if (e->kind == N_CALL && e->fn->builtin == B_PRINTF) { gen_printf_stmt(e); return; }
    if (e->kind == N_CALL && e->fn->builtin == B_PUTCHAR) {
        Node *c = e->kids[0];
        if (c->kind == N_CHAR) {   /* putchar('x') -> print('x', end="") */
            if (c->ival == '\n') line("print()");
            else line("print(%s, end=\"\")", c->str);
            return;
        }
        char *a = xs(c, VAL);
        line("print(chr(%s), end=\"\")", a);
        free(a);
        return;
    }
    if (e->kind == N_PREINC || e->kind == N_POSTINC) {
        char *t = target(e->kids[0]);
        line("%s %c= 1", t, e->str[0]);
        free(t);
        return;
    }
    if (e->kind == N_ASSIGN) {
        Node *lhs = e->kids[0], *rhs = e->kids[1];
        char *t = target(lhs);
        if (e->str[0] == '=') {
            char *v = xs_as(rhs, lhs->type);
            line("%s = %s", t, v);
            free(v);
        } else {
            int int_div = lhs->type == T_INT && rhs->type == T_INT && (e->str[0] == '/' || e->str[0] == '%');
            int narrowing = lhs->type == T_INT && rhs->type == T_DOUBLE;
            if (int_div || narrowing) {
                SB b = {0};
                char op[2] = { e->str[0], 0 };
                gx_compound_value(&b, lhs, op, rhs);
                line("%s = %s", t, sb_str(&b));
                sb_free(&b);
            } else {
                char *v = xs(rhs, VAL);
                line("%s %c= %s", t, e->str[0], v);
                free(v);
            }
        }
        free(t);
        return;
    }
    char *s = xs(e, COND);
    line("%s", s);
    free(s);
}

/* чи є continue, що стосується САМЕ цього циклу */
static int has_continue(Node *n) {
    if (!n) return 0;
    if (n->kind == N_CONTINUE) return 1;
    if (n->kind == N_WHILE || n->kind == N_DOWHILE || n->kind == N_FOR) return 0;
    for (int i = 0; i < n->nkids; i++) if (has_continue(n->kids[i])) return 1;
    return 0;
}

/* чи змінюється змінна s десь у піддереві */
static int modifies(Node *n, Symbol *s) {
    if (!n) return 0;
    if ((n->kind == N_ASSIGN || n->kind == N_PREINC || n->kind == N_POSTINC) &&
        n->kids[0]->kind == N_ID && n->kids[0]->sym == s) return 1;
    for (int i = 0; i < n->nkids; i++) if (modifies(n->kids[i], s)) return 1;
    return 0;
}

/* "чистий" вираз: лише змінні/константи/арифметика, і нічого з них не змінює тіло */
static int pure_in(Node *e, Node *body_) {
    switch (e->kind) {
    case N_INT: case N_CHAR: return 1;
    case N_ID: return e->type == T_INT && !modifies(body_, e->sym);
    case N_BINOP:
        return strchr("+-*", e->str[0]) && e->str[1] == 0 &&
               pure_in(e->kids[0], body_) && pure_in(e->kids[1], body_);
    case N_UNOP: return e->str[0] == '-' && pure_in(e->kids[0], body_);
    default: return 0;
    }
}

/*
 * for (int i = a; i < b; i++)  ->  for i in range(a, b):
 * Тільки якщо це безпечно: i оголошена в циклі, тіло її не змінює, межа не змінюється.
 */
static int try_range_for(Node *n) {
    Node *init = n->kids[0], *c = n->kids[1], *step = n->kids[2], *b = n->kids[3];
    if (init->kind != N_VARDECL || init->nkids != 1) return 0;
    Node *v = init->kids[0];
    if (!v->sym || v->type != T_INT || v->nkids != 1) return 0;
    Symbol *s = v->sym;

    if (c->kind != N_BINOP || c->kids[0]->kind != N_ID || c->kids[0]->sym != s) return 0;
    const char *op = c->str;
    int up;
    if (!strcmp(op, "<") || !strcmp(op, "<=")) up = 1;
    else if (!strcmp(op, ">") || !strcmp(op, ">=")) up = 0;
    else return 0;
    if (!pure_in(c->kids[1], b) || c->kids[1]->type != T_INT) return 0;

    long long delta;
    if ((step->kind == N_POSTINC || step->kind == N_PREINC) && step->kids[0]->kind == N_ID && step->kids[0]->sym == s)
        delta = step->str[0] == '+' ? 1 : -1;
    else if (step->kind == N_ASSIGN && step->kids[0]->kind == N_ID && step->kids[0]->sym == s &&
             step->kids[1]->kind == N_INT && step->kids[1]->ival > 0 &&
             (!strcmp(step->str, "+=") || !strcmp(step->str, "-=")))
        delta = step->str[0] == '+' ? step->kids[1]->ival : -step->kids[1]->ival;
    else return 0;
    if ((delta > 0) != up) return 0;
    if (modifies(b, s)) return 0;

    char *start = xs_as(v->kids[0], T_INT);
    SB stop = {0};
    Node *bound = c->kids[1];
    int inclusive = op[1] == '=';
    if (inclusive && bound->kind == N_INT) sb_printf(&stop, "%lld", bound->ival + (up ? 1 : -1));
    else {
        gx(&stop, bound, inclusive ? P_ADD : P_LOWEST, VAL);
        if (inclusive) sb_printf(&stop, up ? " + 1" : " - 1");
    }

    if (delta == 1 && !strcmp(start, "0")) line("for %s in range(%s):", s->pyname, sb_str(&stop));
    else if (delta == 1) line("for %s in range(%s, %s):", s->pyname, start, sb_str(&stop));
    else line("for %s in range(%s, %s, %lld):", s->pyname, start, sb_str(&stop), delta);
    suite(b);
    free(start);
    sb_free(&stop);
    return 1;
}

static void gen_for(Node *n) {
    if (try_range_for(n)) return;
    Node *init = n->kids[0], *c = n->kids[1], *step = n->kids[2], *b = n->kids[3];
    if (init->kind != N_EMPTY) gs(init);

    if (!has_continue(b)) {
        if (c->kind == N_EMPTY) line("while True:");
        else { char *s = xs(c, COND); line("while %s:", s); free(s); }
        ind++;
        size_t before = body.len;
        gs(b);
        if (step->kind != N_EMPTY) gen_exprstmt(step);
        if (body.len == before) line("pass");
        ind--;
        return;
    }
    /* є continue: крок має виконатись перед наступною перевіркою умови */
    int id = ++uid;
    line("_first%d = True", id);
    line("while True:");
    ind++;
    if (step->kind != N_EMPTY) {
        line("if not _first%d:", id);
        ind++; gen_exprstmt(step); ind--;
    }
    line("_first%d = False", id);
    if (c->kind != N_EMPTY) {
        char *s = xs_not(c);
        line("if %s:", s);
        line("    break");
        free(s);
    }
    gs(b);
    ind--;
}

static void gen_if(Node *n, int elif) {
    char *c = xs(n->kids[0], COND);
    line("%s %s:", elif ? "elif" : "if", c);
    free(c);
    suite(n->kids[1]);
    if (n->nkids > 2) {
        Node *e = n->kids[2];
        if (e->kind == N_IF) gen_if(e, 1);
        else { line("else:"); suite(e); }
    }
}

static void gs(Node *n) {
    switch (n->kind) {
    case N_BLOCK:
        for (int i = 0; i < n->nkids; i++) gs(n->kids[i]);
        break;
    case N_VARDECL: gen_vardecl(n); break;
    case N_EXPRSTMT: gen_exprstmt(n->kids[0]); break;
    case N_EMPTY: break;
    case N_IF: gen_if(n, 0); break;
    case N_WHILE: {
        char *c = xs(n->kids[0], COND);
        line("while %s:", c);
        free(c);
        suite(n->kids[1]);
        break;
    }
    case N_DOWHILE: {
        char *c = xs(n->kids[1], COND);
        if (has_continue(n->kids[0])) {
            int id = ++uid;
            line("_first%d = True", id);
            line("while _first%d or %s:", id, c);
            ind++;
            line("_first%d = False", id);
            gs(n->kids[0]);
            ind--;
        } else {
            char *nc = xs_not(n->kids[1]);
            line("while True:");
            ind++;
            gs(n->kids[0]);
            line("if %s:", nc);
            line("    break");
            ind--;
            free(nc);
        }
        free(c);
        break;
    }
    case N_FOR: gen_for(n); break;
    case N_RETURN:
        if (!n->nkids) line("return");
        else { char *v = xs_as(n->kids[0], cur_fn->ret); line("return %s", v); free(v); }
        break;
    case N_BREAK: line("break"); break;
    case N_CONTINUE: line("continue"); break;
    default: line("# unsupported: %s", kind_name(n->kind));
    }
}

/* глобальні змінні, яким функція щось присвоює -> "global x" */
static void collect_globals(Node *n, Symbol **out, int *cnt) {
    if ((n->kind == N_ASSIGN || n->kind == N_PREINC || n->kind == N_POSTINC) &&
        n->kids[0]->kind == N_ID && n->kids[0]->sym && n->kids[0]->sym->is_global) {
        Symbol *s = n->kids[0]->sym;
        int seen = 0;
        for (int i = 0; i < *cnt; i++) if (out[i] == s) seen = 1;
        if (!seen && *cnt < 256) out[(*cnt)++] = s;
    }
    for (int i = 0; i < n->nkids; i++) collect_globals(n->kids[i], out, cnt);
}

static void gen_func(Node *n) {
    FuncInfo *f = n->fn;
    if (!f || f->def != n) return;
    cur_fn = f;
    SB b = {0};
    for (int i = 0; i < f->nparams; i++) {
        if (i) sb_printf(&b, ", ");
        sb_printf(&b, "%s", f->params[i] ? f->params[i]->pyname : "_");
    }
    line("def %s(%s):", f->pyname, sb_str(&b));
    sb_free(&b);
    ind++;

    Symbol *g[256];
    int cnt = 0;
    collect_globals(n->kids[1], g, &cnt);
    if (cnt) {
        SB gl = {0};
        for (int i = 0; i < cnt; i++) sb_printf(&gl, "%s%s", i ? ", " : "", g[i]->pyname);
        line("global %s", sb_str(&gl));
        sb_free(&gl);
    }
    size_t before = body.len;
    gs(n->kids[1]);
    if (body.len == before) line("pass");
    ind--;
    cur_fn = NULL;
}

void gen_python(FILE *out, Node *prog, const char *srcname) {
    sb_free(&body);
    ind = 0;
    need_idiv = need_imod = need_set = need_post = need_printf = need_putchar = need_math = 0;

    int prev_func = 0;
    for (int i = 0; i < prog->nkids; i++) {
        Node *n = prog->kids[i];
        if (n->kind == N_VARDECL) {
            if (prev_func) sb_printf(&body, "\n");
            gen_vardecl(n);
            prev_func = 0;
        } else if (n->kind == N_FUNC) {
            sb_printf(&body, "\n\n");
            gen_func(n);
            prev_func = 1;
        }
    }

    FuncInfo *m = find_func("main");
    if (m && m->def) {
        sb_printf(&body, "\n\nif __name__ == \"__main__\":\n");
        if (m->ret == T_VOID) sb_printf(&body, "    %s()\n", m->pyname);
        else sb_printf(&body, "    raise SystemExit(%s())\n", m->pyname);
    }

    fprintf(out, "# Згенеровано транслятором c2py з файлу %s\n", srcname);
    fprintf(out, "# (C -> Python 3)\n");
    if (need_math) fprintf(out, "import math\n");
    int any = need_idiv || need_imod || need_set || need_post || need_printf || need_putchar;
    if (any) fprintf(out, "\n\n# --- допоміжні функції для семантики C ---\n");
    if (need_idiv || need_imod)
        fprintf(out, "def _idiv(a, b):\n"
                     "    # ділення як у C: результат округлюється до нуля\n"
                     "    q = abs(a) // abs(b)\n"
                     "    return q if (a >= 0) == (b >= 0) else -q\n\n");
    if (need_imod)
        fprintf(out, "def _imod(a, b):\n"
                     "    return a - b * _idiv(a, b)\n\n");
    if (need_set)
        fprintf(out, "def _set(arr, i, v):\n"
                     "    arr[i] = v\n"
                     "    return v\n\n");
    if (need_post)
        fprintf(out, "def _post(arr, i, d):\n"
                     "    old = arr[i]\n"
                     "    arr[i] = old + d\n"
                     "    return old\n\n");
    if (need_printf)
        fprintf(out, "def _printf(fmt, *args):\n"
                     "    s = fmt %% args\n"
                     "    print(s, end=\"\")\n"
                     "    return len(s)\n\n");
    if (need_putchar)
        fprintf(out, "def _putchar(c):\n"
                     "    print(chr(c), end=\"\")\n"
                     "    return c\n\n");

    /* прибираємо зайві порожні рядки на початку тіла */
    const char *s = sb_str(&body);
    if (!any) while (*s == '\n') s++;
    else { while (*s == '\n') s++; fputs("\n", out); }
    fputs(s, out);
}
