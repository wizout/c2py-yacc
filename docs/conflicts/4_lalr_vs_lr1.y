/* Граматика, яка є LR(1), але НЕ LALR(1).
 * У канонічному LR(1) стани після "a e" і "b e" різні, а LALR зливає їх
 * (однакове ядро E -> e. , F -> e.) і об'єднує lookahead-и: {c,d} для обох
 * -> з'являється reduce/reduce конфлікт, якого в LR(1) немає.
 *
 *   bison 4_lalr_vs_lr1.y                          -> 2 reduce/reduce (на C і на D)
 *   bison -Dlr.type=canonical-lr 4_lalr_vs_lr1.y   -> 0 конфліктів
 */
%token A B C D E
%%
s : A e C
  | A f D
  | B f C
  | B e D
  ;
e : E ;
f : E ;
