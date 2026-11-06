#include <stdio.h>
int main() {
    int i = 0, s = 0;
    for (;;) {
        if (++i > 5) break;
        s += i;
    }
    printf("%d %d\n", i, s);

    int n = 0;
    do {
        n++;
        if (n % 2) continue;
        printf("even %d\n", n);
    } while (n < 6);

    for (int i = 10; i > 0; i -= 3) printf("%d ", i);
    printf("\n");
    for (int i = 0; i < 3; i++)
        for (int i = 0; i < 2; i++)
            printf("[%d]", i);
    printf("\n");
    int k = 0;
    while (k < 3) k++;
    for (int m = k; m >= 0; m--) printf("%d", m);
    printf("\n");
    int lim = 4;
    for (int q = 0; q < lim; q++) { if (q == 1) lim = 2; printf("q%d ", q); }
    printf("\n");
    return 0;
}
