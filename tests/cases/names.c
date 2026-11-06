#include <stdio.h>
int print = 3;
int len(int lambda) { return lambda * 2; }
int main() {
    int def = 4, list = 5;
    int x = 1;
    if (x) { int x = 2; { int x = 3; printf("%d", x); } printf("%d", x); }
    printf("%d\n", x);
    printf("%d %d\n", len(def) + print, list);
    print = print + 1;
    printf("%d\n", print);
    return 0;
}
