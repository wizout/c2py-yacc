/*
 * c2py — транслятор підмножини C.
 *
 *   c2py [режим] файл.c
 *
 *   --tokens   потік токенів (результат лексичного аналізу)
 *   --ast      AST у вигляді дерева в терміналі
 *   --json     AST у форматі JSON (для веб-візуалізації)
 *   --run      виконати програму інтерпретатором (за замовчуванням)
 *   --py       згенерувати еквівалентну програму на Python
 *   -o FILE    куди писати результат (для --py / --json / --ast)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "sema.h"
#include "interp.h"
#include "codegen.h"
#include "parser.h"

extern FILE *yyin;
extern int yylineno;
extern char *yytext;
extern int lex_errors, parse_errors;
extern Node *ast_root;
int yylex(void);
const char *token_name(int tok);

static void usage(void) {
    fprintf(stderr,
        "usage: c2py [--tokens | --ast | --json | --run | --py] [-o out] file.c\n"
        "  --tokens  print token stream\n"
        "  --ast     print syntax tree\n"
        "  --json    print syntax tree as JSON\n"
        "  --run     interpret the program (default)\n"
        "  --py      translate to Python 3\n");
    exit(2);
}

static void dump_tokens(FILE *out) {
    int t;
    while ((t = yylex()) != 0)
        fprintf(out, "%4d  %-14s %s\n", yylineno, token_name(t), yytext);
}

int main(int argc, char **argv) {
    const char *mode = "--run", *file = NULL, *outname = NULL;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < argc) outname = argv[++i];
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) usage();
        else if (argv[i][0] == '-' && argv[i][1] == '-') mode = argv[i];
        else file = argv[i];
    }
    if (!file) usage();

    if (!strcmp(file, "-")) yyin = stdin;
    else if (!(yyin = fopen(file, "r"))) { perror(file); return 2; }

    FILE *out = stdout;
    if (outname && !(out = fopen(outname, "w"))) { perror(outname); return 2; }

    if (!strcmp(mode, "--tokens")) {
        dump_tokens(out);
        return lex_errors ? 1 : 0;
    }

    /* 1. синтаксичний аналіз (лексер викликається парсером) */
    yyparse();
    if (lex_errors || parse_errors || !ast_root) {
        fprintf(stderr, "%d error(s), stopped.\n", lex_errors + parse_errors);
        return 1;
    }

    /* 2. семантичний аналіз */
    int sem = sema_check(ast_root);

    /* дерево показуємо навіть якщо є семантичні помилки — так зручніше дивитись */
    if (!strcmp(mode, "--ast")) { ast_print(out, ast_root); return sem ? 1 : 0; }
    if (!strcmp(mode, "--json")) { ast_json(out, ast_root); return sem ? 1 : 0; }

    if (sem) {
        fprintf(stderr, "%d semantic error(s), stopped.\n", sem);
        return 1;
    }

    /* 3. генерація коду або виконання */
    if (!strcmp(mode, "--py")) {
        gen_python(out, ast_root, strcmp(file, "-") ? file : "stdin");
        return 0;
    }
    if (!strcmp(mode, "--run")) {
        int code = interp_run(ast_root);
        return code < 0 ? 1 : code;
    }
    usage();
    return 2;
}
