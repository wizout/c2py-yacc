#include <stdio.h>

/* рекурсія і цикл: два способи порахувати n! */

long fact_rec(int n) {
    if (n <= 1)
        return 1;
    return n * fact_rec(n - 1);
}

long fact_iter(int n) {
    long r = 1;
    for (int i = 2; i <= n; i++)
        r *= i;
    return r;
}

int main() {
    for (int n = 0; n <= 12; n++) {
        printf("%2d! = %ld", n, fact_rec(n));
        if (fact_rec(n) != fact_iter(n))
            printf("  <- mismatch!");
        printf("\n");
    }
    return 0;
}
