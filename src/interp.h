#ifndef INTERP_H
#define INTERP_H

#include "ast.h"

/* виконує main(); повертає код завершення або -1 при помилці виконання */
int interp_run(Node *program);

#endif
