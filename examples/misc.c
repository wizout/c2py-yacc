#include <stdio.h>

int calls = 0;   /* глобальна змінна, яку змінюють функції */

int gcd(int a, int b) {
    calls++;
    return b == 0 ? a : gcd(b, a % b);
}

int fib(int n) {
    int a = 0, b = 1;
    while (n-- > 0) {
        int t = a + b;
        a = b;
        b = t;
    }
    return a;
}

int digits_sum(int x) {
    int s = 0;
    do {
        s += x % 10;
        x /= 10;
    } while (x > 0);
    return s;
}

char grade(int score) {
    if (score >= 90) return 'A';
    else if (score >= 75) return 'B';
    else if (score >= 60) return 'C';
    else return 'F';
}

int main() {
    int g = gcd(84, 36);
    printf("gcd(84, 36) = %d (calls: %d)\n", g, calls);

    for (int i = 0; i <= 10; i++)
        printf("%d ", fib(i));
    printf("\n");

    printf("digits_sum(98765) = %d\n", digits_sum(98765));

    int scores[4] = {95, 80, 61, 12};
    for (int i = 0; i < 4; i++) {
        putchar(grade(scores[i]));
        putchar(' ');
    }
    putchar('\n');

    /* висячий else: else належить внутрішньому if */
    int x = 5;
    if (x > 0)
        if (x > 10)
            printf("big\n");
        else
            printf("small positive\n");

    /* ++ і присвоєння всередині виразів */
    int arr[5] = {0};
    int i = 0;
    arr[i++] = 10;
    arr[i++] = 20;
    arr[++i] = 40;
    int y, z;
    y = z = i * 3;
    printf("arr = %d %d %d %d %d, i = %d, y = %d, z = %d\n", arr[0], arr[1], arr[2], arr[3], arr[4], i, y, z);

    /* зовнішня і внутрішня змінна з одним ім'ям */
    int v = 1;
    {
        int v = 2;
        v += 10;
        printf("inner v = %d\n", v);
    }
    printf("outer v = %d\n", v);

    /* логічні вирази як числа */
    int t = (3 < 5) + (2 == 2) + !0 + (1 && 0) + (0 || 7);
    printf("t = %d\n", t);

    /* for з continue: крок повинен виконуватись */
    int odd = 0;
    for (int k = 0; k < 10; k += 1) {
        if (k % 2 == 0) continue;
        odd += k;
    }
    int j;
    for (j = 0; j < 20; j++) {
        if (j % 3) continue;
        if (j > 12) break;
    }
    printf("odd sum = %d, j = %d\n", odd, j);
    return 0;
}
