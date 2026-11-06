/* "Висячий else": if (a) if (b) x; else y;
 * else можна приєднати до будь-якого з двох if -> 1 shift/reduce конфлікт.
 * bison за замовчуванням обирає shift — else іде до найближчого if (як у C).
 * У src/parser.y конфлікт прибрано явно через %precedence LOWER_THAN_ELSE / ELSE. */
%token IF ELSE COND OTHER
%%
stmt : IF COND stmt
     | IF COND stmt ELSE stmt
     | OTHER
     ;
