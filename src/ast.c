#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "ast.h"

void *xmalloc(size_t n) {
    void *p = calloc(1, n);
    if (!p) { fprintf(stderr, "out of memory\n"); exit(2); }
    return p;
}

char *xstrdup(const char *s) {
    if (!s) return NULL;
    char *r = xmalloc(strlen(s) + 1);
    strcpy(r, s);
    return r;
}

Node *node_new(NodeKind k, int line) {
    Node *n = xmalloc(sizeof(Node));
    n->kind = k;
    n->line = line;
    return n;
}

Node *node_add(Node *p, Node *c) {
    if (!c) return p;
    if (p->nkids == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 4;
        p->kids = realloc(p->kids, p->cap * sizeof(Node *));
    }
    p->kids[p->nkids++] = c;
    return p;
}

Node *mk(NodeKind k, int line, int n, ...) {
    Node *r = node_new(k, line);
    va_list ap;
    va_start(ap, n);
    for (int i = 0; i < n; i++) node_add(r, va_arg(ap, Node *));
    va_end(ap);
    return r;
}

Node *mk_str(NodeKind k, int line, const char *s) {
    Node *r = node_new(k, line);
    r->str = xstrdup(s);
    return r;
}

static const char *KIND_NAMES[] = {
    "Program", "FuncDef", "FuncProto", "Params", "Param", "Block", "VarDecl", "Var",
    "InitList", "If", "While", "DoWhile", "For", "Return", "Break", "Continue",
    "Empty", "ExprStmt", "Assign", "BinOp", "UnOp", "PreInc", "PostInc", "Ternary",
    "Call", "Index", "Cast", "Id", "Int", "Double", "Char", "String"
};

const char *kind_name(NodeKind k) { return KIND_NAMES[k]; }

const char *type_name(Type t) {
    switch (t) {
    case T_INT: return "int";
    case T_DOUBLE: return "double";
    case T_VOID: return "void";
    case T_INT_ARR: return "int[]";
    case T_DBL_ARR: return "double[]";
    case T_STR: return "char*";
    default: return "";
    }
}

/* char, long, short трактуємо як int; float як double */
Type type_from_name(const char *t) {
    if (!t) return T_NONE;
    if (!strcmp(t, "double") || !strcmp(t, "float")) return T_DOUBLE;
    if (!strcmp(t, "void")) return T_VOID;
    return T_INT;
}

/* короткий підпис вузла: те, що буде написано в кружечку на дереві */
static void node_label(Node *n, char *buf, size_t sz) {
    switch (n->kind) {
    case N_FUNC: snprintf(buf, sz, "%s %s()", n->tname, n->str); break;
    case N_PROTO: snprintf(buf, sz, "proto %s()", n->str); break;
    case N_PARAM: snprintf(buf, sz, "%s %s%s", n->tname, n->str, n->ival ? "[]" : ""); break;
    case N_VARDECL: snprintf(buf, sz, "decl %s", n->tname); break;
    case N_VAR:
        if (n->ival > 0) snprintf(buf, sz, "%s[%lld]", n->str, n->ival);
        else if (n->ival == 0) snprintf(buf, sz, "%s[]", n->str);
        else snprintf(buf, sz, "%s", n->str);
        break;
    case N_ASSIGN: case N_BINOP: case N_UNOP:
        snprintf(buf, sz, "%s", n->str); break;
    case N_PREINC: snprintf(buf, sz, "%sx", n->str); break;
    case N_POSTINC: snprintf(buf, sz, "x%s", n->str); break;
    case N_TERNARY: snprintf(buf, sz, "?:"); break;
    case N_CALL: snprintf(buf, sz, "%s()", n->str); break;
    case N_INDEX: snprintf(buf, sz, "[ ]"); break;
    case N_CAST: snprintf(buf, sz, "(%s)", n->tname); break;
    case N_ID: snprintf(buf, sz, "%s", n->str); break;
    case N_INT: snprintf(buf, sz, "%lld", n->ival); break;
    case N_DOUBLE: snprintf(buf, sz, "%g", n->dval); break;
    case N_CHAR: snprintf(buf, sz, "%s", n->str); break;
    case N_STRING: snprintf(buf, sz, "\"%s\"", n->str); break;
    case N_IF: snprintf(buf, sz, "if"); break;
    case N_WHILE: snprintf(buf, sz, "while"); break;
    case N_DOWHILE: snprintf(buf, sz, "do-while"); break;
    case N_FOR: snprintf(buf, sz, "for"); break;
    case N_RETURN: snprintf(buf, sz, "return"); break;
    case N_BREAK: snprintf(buf, sz, "break"); break;
    case N_CONTINUE: snprintf(buf, sz, "continue"); break;
    case N_BLOCK: snprintf(buf, sz, "{ }"); break;
    case N_EMPTY: snprintf(buf, sz, "∅"); break;
    case N_EXPRSTMT: snprintf(buf, sz, "expr;"); break;
    default: snprintf(buf, sz, "%s", kind_name(n->kind));
    }
}

static void print_rec(FILE *out, Node *n, const char *prefix, int last, int root) {
    char label[256];
    node_label(n, label, sizeof label);
    if (root) fprintf(out, "%s", kind_name(n->kind));
    else fprintf(out, "%s%s%s", prefix, last ? "└── " : "├── ", kind_name(n->kind));
    if (strcmp(label, kind_name(n->kind)) != 0) fprintf(out, " [%s]", label);
    if (n->type != T_NONE) fprintf(out, " : %s", type_name(n->type));
    fprintf(out, "  (line %d)\n", n->line);

    char np[1024];
    snprintf(np, sizeof np, "%s%s", prefix, root ? "" : (last ? "    " : "│   "));
    for (int i = 0; i < n->nkids; i++)
        print_rec(out, n->kids[i], np, i == n->nkids - 1, 0);
}

void ast_print(FILE *out, Node *n) { print_rec(out, n, "", 1, 1); }

static void json_str(FILE *out, const char *s) {
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = *s;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c == '\n') fputs("\\n", out);
        else if (c == '\t') fputs("\\t", out);
        else if (c < 0x20) fprintf(out, "\\u%04x", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

static void json_rec(FILE *out, Node *n) {
    char label[256];
    node_label(n, label, sizeof label);
    fprintf(out, "{\"kind\":\"%s\",\"label\":", kind_name(n->kind));
    json_str(out, label);
    fprintf(out, ",\"line\":%d", n->line);
    if (n->type != T_NONE) fprintf(out, ",\"type\":\"%s\"", type_name(n->type));
    if (n->nkids) {
        fprintf(out, ",\"children\":[");
        for (int i = 0; i < n->nkids; i++) {
            if (i) fputc(',', out);
            json_rec(out, n->kids[i]);
        }
        fputc(']', out);
    }
    fputc('}', out);
}

void ast_json(FILE *out, Node *n) {
    json_rec(out, n);
    fputc('\n', out);
}
