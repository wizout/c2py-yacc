#include <stdio.h>
#include <math.h>

/* дійсні числа: метод Ньютона для кореня, ряд для e, перетворення типів */

double my_sqrt(double x) {
    double r = x;
    if (x == 0) return 0;
    for (int i = 0; i < 30; i++)
        r = (r + x / r) / 2;
    return r;
}

double euler(int terms) {
    double e = 0, term = 1;
    for (int k = 0; k < terms; k++) {
        e += term;
        term /= k + 1;
    }
    return e;
}

int main() {
    double xs[5] = {2, 9, 10.5, 144, 0.25};
    for (int i = 0; i < 5; i++)
        printf("sqrt(%g): mine = %.10f, libm = %.10f\n", xs[i], my_sqrt(xs[i]), sqrt(xs[i]));

    printf("e ~ %.12f\n", euler(20));
    printf("2^10 = %.0f\n", pow(2, 10));

    int a = 7, b = 2;
    printf("7 / 2 = %d, (double)7 / 2 = %.1f\n", a / b, (double)a / b);
    printf("-7 / 2 = %d, -7 %% 2 = %d\n", -a / b, -a % b);

    double avg = (a + b) / 2;     /* ціле ділення, потім у double */
    int trunc = 9.99;             /* double -> int відкидає дробову частину */
    printf("avg = %.2f, trunc = %d\n", avg, trunc);
    return 0;
}
