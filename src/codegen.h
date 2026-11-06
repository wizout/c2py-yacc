#ifndef CODEGEN_H
#define CODEGEN_H

#include <stdio.h>
#include "ast.h"

/* трансляція AST (після семантики) у програму на Python 3 */
void gen_python(FILE *out, Node *program, const char *srcname);

#endif
