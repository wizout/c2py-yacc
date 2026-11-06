%{
/*
 * Лексичний аналізатор для підмножини мови C.
 * Генерується flex'ом у lexer.c, викликається з парсера через yylex().
 */
#include <stdlib.h>
#include <string.h>
#include "ast.h"
#include "parser.h"

int lex_errors = 0;

/* вміст рядка/символа без лапок */
static char *strip_quotes(const char *s) {
    size_t n = strlen(s);
    char *r = xstrdup(s + 1);
    r[n - 2] = '\0';
    return r;
}

/* код символьного літерала: 'a', '\n', '\0', '\'' ... */
static int char_code(const char *s) {
    if (s[1] != '\\') return (unsigned char)s[1];
    switch (s[2]) {
    case 'n': return '\n';
    case 't': return '\t';
    case 'r': return '\r';
    case '0': return 0;
    case 'a': return '\a';
    case 'b': return '\b';
    case 'f': return '\f';
    case 'v': return '\v';
    default:  return (unsigned char)s[2];   /* \\  \'  \" */
    }
}
%}

%option noyywrap nounput noinput yylineno

D       [0-9]
L       [a-zA-Z_]
H       [0-9a-fA-F]
E       [eE][+-]?{D}+

%x COMMENT

%%

"/*"                    { BEGIN(COMMENT); }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>\n             { }
<COMMENT>.              { }
<COMMENT><<EOF>>        { fprintf(stderr, "line %d: error: unterminated comment\n", yylineno);
                          lex_errors++; BEGIN(INITIAL); return 0; }
"//".*                  { /* однорядковий коментар */ }
^[ \t]*#.*              { /* #include, #define — препроцесор не підтримуємо, просто пропускаємо */ }

"int"                   { return INT; }
"char"                  { return CHAR; }
"long"                  { return LONG; }
"short"                 { return SHORT; }
"float"                 { return FLOAT; }
"double"                { return DOUBLE; }
"void"                  { return VOID; }
"if"                    { return IF; }
"else"                  { return ELSE; }
"while"                 { return WHILE; }
"do"                    { return DO; }
"for"                   { return FOR; }
"return"                { return RETURN; }
"break"                 { return BREAK; }
"continue"              { return CONTINUE; }

{L}({L}|{D})*           { yylval.s = xstrdup(yytext); return ID; }

0[xX]{H}+               { yylval.i = strtoll(yytext, NULL, 16); return INT_LIT; }
{D}+                    { yylval.i = strtoll(yytext, NULL, 10); return INT_LIT; }

{D}+{E}[fFlL]?          |
{D}*"."{D}+({E})?[fFlL]? |
{D}+"."{D}*({E})?[fFlL]? { yylval.d = strtod(yytext, NULL); return DOUBLE_LIT; }

'(\\.|[^\\'\n])'        { yylval.ch.code = char_code(yytext); yylval.ch.text = xstrdup(yytext); return CHAR_LIT; }
\"(\\.|[^\\"\n])*\"     { yylval.s = strip_quotes(yytext); return STRING_LIT; }

"+="                    { return ADD_ASSIGN; }
"-="                    { return SUB_ASSIGN; }
"*="                    { return MUL_ASSIGN; }
"/="                    { return DIV_ASSIGN; }
"%="                    { return MOD_ASSIGN; }
"++"                    { return INC; }
"--"                    { return DEC; }
"&&"                    { return AND; }
"||"                    { return OR; }
"=="                    { return EQ; }
"!="                    { return NE; }
"<="                    { return LE; }
">="                    { return GE; }

[-+*/%<>=!?:;,(){}\[\]] { return yytext[0]; }

[ \t\r\n]+              { /* пробіли */ }

.                       { fprintf(stderr, "line %d: error: unexpected character '%s'\n", yylineno, yytext);
                          lex_errors++; }

%%
