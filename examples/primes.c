#include <stdio.h>

#define N 100

/* решето Ератосфена */
int sieve[101];

int main(void) {
    int count = 0;

    for (int i = 2; i <= 100; i++)
        sieve[i] = 1;

    for (int i = 2; i * i <= 100; i++) {
        if (!sieve[i])
            continue;
        for (int j = i * i; j <= 100; j += i)
            sieve[j] = 0;
    }

    printf("primes up to 100:\n");
    for (int i = 2; i <= 100; i++) {
        if (sieve[i]) {
            printf("%d ", i);
            count++;
            if (count % 10 == 0) printf("\n");
        }
    }
    printf("\ntotal: %d\n", count);

    /* перше просте число більше за 1000 — while + break */
    int k = 1000;
    while (1) {
        k++;
        int is_prime = 1;
        for (int d = 2; d * d <= k; d++) {
            if (k % d == 0) {
                is_prime = 0;
                break;
            }
        }
        if (is_prime) break;
    }
    printf("first prime after 1000: %d\n", k);
    return 0;
}
