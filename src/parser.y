%{
/*
 * Синтаксичний аналізатор (LALR(1)) для підмножини мови C.
 * bison будує таблиці ACTION/GOTO, а дії в фігурних дужках будують AST.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

int yylex(void);
void yyerror(const char *msg);

extern int yylineno;
extern char *yytext;

Node *ast_root = NULL;
int parse_errors = 0;

/* хелпери, щоб дії в правилах були коротші */
static Node *binop(const char *op, Node *a, Node *b) {
    Node *n = mk(N_BINOP, a->line, 2, a, b);
    n->str = xstrdup(op);
    return n;
}
static Node *assign(const char *op, Node *a, Node *b) {
    Node *n = mk(N_ASSIGN, a->line, 2, a, b);
    n->str = xstrdup(op);
    return n;
}
static Node *unop(NodeKind k, const char *op, Node *a) {
    Node *n = mk(k, a->line, 1, a);
    n->str = xstrdup(op);
    return n;
}
%}

%code requires {
#include "ast.h"
}

%union {
    long long i;
    double d;
    char *s;
    struct { int code; char *text; } ch;
    Node *n;
}

/* parse.error verbose — щоб у повідомленні про помилку було видно, що очікувалось */
%define parse.error verbose

%token <s> ID STRING_LIT
%token <i> INT_LIT
%token <d> DOUBLE_LIT
%token <ch> CHAR_LIT
%token INT CHAR LONG SHORT FLOAT DOUBLE VOID
%token IF ELSE WHILE DO FOR RETURN BREAK CONTINUE
%token ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN

/*
 * Пріоритети й асоціативність — від найнижчого до найвищого.
 * Саме ці оголошення розв'язують shift/reduce конфлікти в граматиці виразів
 * (без них bison видав би кілька сотень конфліктів).
 */
%right '=' ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN MOD_ASSIGN
%right '?' ':'
%left OR
%left AND
%left EQ NE
%left '<' '>' LE GE
%left '+' '-'
%left '*' '/' '%'
%precedence UNARY            /* фіктивний токен для унарного мінуса, !, префіксних ++/--, cast */
%precedence INC DEC          /* постфіксні ++ і -- зв'язують найсильніше */

/* класичний "висячий else": else прив'язується до найближчого if */
%precedence LOWER_THAN_ELSE
%precedence ELSE

%type <n> program ext_list ext func_def func_proto params param_list param
%type <n> var_decl var_list var init init_list block stmt_list stmt
%type <n> for_init opt_expr expr args arg_list string
%type <s> type

%start program

%%

program
    : ext_list                  { ast_root = $1; $$ = $1; }
    ;

ext_list
    : %empty                    { $$ = node_new(N_PROGRAM, 1); }
    | ext_list ext              { $$ = node_add($1, $2); }
    ;

ext
    : func_def
    | func_proto
    | var_decl
    | error ';'                 { $$ = NULL; yyerrok; }
    | error '}'                 { $$ = NULL; yyerrok; }
    ;

type
    : INT       { $$ = "int"; }
    | CHAR      { $$ = "char"; }
    | LONG      { $$ = "long"; }
    | SHORT     { $$ = "short"; }
    | FLOAT     { $$ = "float"; }
    | DOUBLE    { $$ = "double"; }
    | VOID      { $$ = "void"; }
    ;

func_def
    : type ID '(' params ')' block {
          $$ = mk(N_FUNC, $6->line, 2, $4, $6);
          $$->str = $2;
          $$->tname = xstrdup($1);
          $$->line = $4->line;
      }
    ;

func_proto
    : type ID '(' params ')' ';' {
          $$ = mk(N_PROTO, $4->line, 1, $4);
          $$->str = $2;
          $$->tname = xstrdup($1);
      }
    ;

params
    : %empty                    { $$ = node_new(N_PARAMS, yylineno); }
    | VOID                      { $$ = node_new(N_PARAMS, yylineno); }
    | param_list
    ;

param_list
    : param                     { $$ = mk(N_PARAMS, $1->line, 1, $1); }
    | param_list ',' param      { $$ = node_add($1, $3); }
    ;

param
    : type ID                   { $$ = mk_str(N_PARAM, yylineno, $2); $$->tname = xstrdup($1); }
    | type ID '[' ']'           { $$ = mk_str(N_PARAM, yylineno, $2); $$->tname = xstrdup($1); $$->ival = 1; }
    | type ID '[' INT_LIT ']'   { $$ = mk_str(N_PARAM, yylineno, $2); $$->tname = xstrdup($1); $$->ival = 1; }
    ;

var_decl
    : type var_list ';'         { $$ = $2; $$->tname = xstrdup($1); }
    ;

var_list
    : var                       { $$ = mk(N_VARDECL, $1->line, 1, $1); }
    | var_list ',' var          { $$ = node_add($1, $3); }
    ;

var
    : ID                        { $$ = mk_str(N_VAR, yylineno, $1); $$->ival = -1; }
    | ID '=' init               { $$ = mk_str(N_VAR, yylineno, $1); $$->ival = -1; node_add($$, $3); }
    | ID '[' INT_LIT ']'        { $$ = mk_str(N_VAR, yylineno, $1); $$->ival = $3; }
    | ID '[' INT_LIT ']' '=' init { $$ = mk_str(N_VAR, yylineno, $1); $$->ival = $3; node_add($$, $6); }
    | ID '[' ']' '=' init       { $$ = mk_str(N_VAR, yylineno, $1); $$->ival = 0; node_add($$, $5); }
    ;

init
    : expr
    | '{' init_list '}'         { $$ = $2; }
    | '{' init_list ',' '}'     { $$ = $2; }
    ;

init_list
    : expr                      { $$ = mk(N_INITLIST, $1->line, 1, $1); }
    | init_list ',' expr        { $$ = node_add($1, $3); }
    ;

block
    : '{' stmt_list '}'         { $$ = $2; }
    ;

/* оголошення і оператори можна змішувати (як у C99) */
stmt_list
    : %empty                    { $$ = node_new(N_BLOCK, yylineno); }
    | stmt_list stmt            { $$ = node_add($1, $2); }
    | stmt_list var_decl        { $$ = node_add($1, $2); }
    ;

stmt
    : block
    | expr ';'                  { $$ = mk(N_EXPRSTMT, $1->line, 1, $1); }
    | ';'                       { $$ = node_new(N_EMPTY, yylineno); }
    | IF '(' expr ')' stmt %prec LOWER_THAN_ELSE
                                { $$ = mk(N_IF, $3->line, 2, $3, $5); }
    | IF '(' expr ')' stmt ELSE stmt
                                { $$ = mk(N_IF, $3->line, 3, $3, $5, $7); }
    | WHILE '(' expr ')' stmt   { $$ = mk(N_WHILE, $3->line, 2, $3, $5); }
    | DO stmt WHILE '(' expr ')' ';'
                                { $$ = mk(N_DOWHILE, $5->line, 2, $2, $5); }
    | FOR '(' for_init opt_expr ';' opt_expr ')' stmt
                                { $$ = mk(N_FOR, $3->line, 4, $3, $4, $6, $8); }
    | RETURN opt_expr ';'       { $$ = node_new(N_RETURN, yylineno);
                                  if ($2->kind != N_EMPTY) node_add($$, $2); }
    | BREAK ';'                 { $$ = node_new(N_BREAK, yylineno); }
    | CONTINUE ';'              { $$ = node_new(N_CONTINUE, yylineno); }
    | error ';'                 { $$ = node_new(N_EMPTY, yylineno); yyerrok; }
    ;

for_init
    : ';'                       { $$ = node_new(N_EMPTY, yylineno); }
    | expr ';'                  { $$ = mk(N_EXPRSTMT, $1->line, 1, $1); }
    | var_decl
    ;

opt_expr
    : %empty                    { $$ = node_new(N_EMPTY, yylineno); }
    | expr
    ;

expr
    : expr '=' expr             { $$ = assign("=", $1, $3); }
    | expr ADD_ASSIGN expr      { $$ = assign("+=", $1, $3); }
    | expr SUB_ASSIGN expr      { $$ = assign("-=", $1, $3); }
    | expr MUL_ASSIGN expr      { $$ = assign("*=", $1, $3); }
    | expr DIV_ASSIGN expr      { $$ = assign("/=", $1, $3); }
    | expr MOD_ASSIGN expr      { $$ = assign("%=", $1, $3); }
    | expr '?' expr ':' expr    { $$ = mk(N_TERNARY, $1->line, 3, $1, $3, $5); }
    | expr OR expr              { $$ = binop("||", $1, $3); }
    | expr AND expr             { $$ = binop("&&", $1, $3); }
    | expr EQ expr              { $$ = binop("==", $1, $3); }
    | expr NE expr              { $$ = binop("!=", $1, $3); }
    | expr '<' expr             { $$ = binop("<", $1, $3); }
    | expr '>' expr             { $$ = binop(">", $1, $3); }
    | expr LE expr              { $$ = binop("<=", $1, $3); }
    | expr GE expr              { $$ = binop(">=", $1, $3); }
    | expr '+' expr             { $$ = binop("+", $1, $3); }
    | expr '-' expr             { $$ = binop("-", $1, $3); }
    | expr '*' expr             { $$ = binop("*", $1, $3); }
    | expr '/' expr             { $$ = binop("/", $1, $3); }
    | expr '%' expr             { $$ = binop("%", $1, $3); }
    | '-' expr %prec UNARY      { $$ = unop(N_UNOP, "-", $2); }
    | '+' expr %prec UNARY      { $$ = unop(N_UNOP, "+", $2); }
    | '!' expr %prec UNARY      { $$ = unop(N_UNOP, "!", $2); }
    | INC expr %prec UNARY      { $$ = unop(N_PREINC, "++", $2); }
    | DEC expr %prec UNARY      { $$ = unop(N_PREINC, "--", $2); }
    | expr INC                  { $$ = unop(N_POSTINC, "++", $1); }
    | expr DEC                  { $$ = unop(N_POSTINC, "--", $1); }
    | '(' type ')' expr %prec UNARY
                                { $$ = mk(N_CAST, $4->line, 1, $4); $$->tname = xstrdup($2); }
    | '(' expr ')'              { $$ = $2; }
    | ID '(' args ')'           { $$ = $3; $$->str = $1; }
    | ID '[' expr ']'           { $$ = mk(N_INDEX, $3->line, 2, mk_str(N_ID, $3->line, $1), $3); }
    | ID                        { $$ = mk_str(N_ID, yylineno, $1); }
    | INT_LIT                   { $$ = node_new(N_INT, yylineno); $$->ival = $1; }
    | DOUBLE_LIT                { $$ = node_new(N_DOUBLE, yylineno); $$->dval = $1; }
    | CHAR_LIT                  { $$ = node_new(N_CHAR, yylineno); $$->ival = $1.code; $$->str = $1.text; }
    | string
    ;

/* "abc" "def" — сусідні рядки склеюються, як у C */
string
    : STRING_LIT                { $$ = mk_str(N_STRING, yylineno, $1); }
    | string STRING_LIT         { char *s = xmalloc(strlen($1->str) + strlen($2) + 1);
                                  strcpy(s, $1->str); strcat(s, $2);
                                  $1->str = s; $$ = $1; }
    ;

args
    : %empty                    { $$ = node_new(N_CALL, yylineno); }
    | arg_list
    ;

arg_list
    : expr                      { $$ = mk(N_CALL, $1->line, 1, $1); }
    | arg_list ',' expr         { $$ = node_add($1, $3); }
    ;

%%

void yyerror(const char *msg) {
    parse_errors++;
    fprintf(stderr, "line %d: %s (near '%s')\n", yylineno, msg, yytext);
}

/* ім'я токена — для режиму --tokens */
const char *token_name(int tok) {
    return yysymbol_name(YYTRANSLATE(tok));
}
