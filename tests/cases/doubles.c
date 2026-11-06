#include <stdio.h>
double avg(double a[], int n) {
    double s = 0;
    for (int i = 0; i < n; i++) s += a[i];
    return s / n;
}
int to_int(double d) { return d; }
int main() {
    double v[4] = {1.5, 2.25, -3, 10};
    printf("%.4f\n", avg(v, 4));
    int k = 7;
    k *= 1.5;
    printf("%d\n", k);
    k /= 2;
    printf("%d %d\n", k, to_int(-2.7));
    double d = 1;
    d++;
    d /= 4;
    printf("%f %e %g\n", d, d * 1000000, d);
    printf("%d %d %d\n", -17 / 5, -17 % 5, 17 % -5);
    printf("%c%c%c\n", 'a' + 1, 'z' - 25, 65);
    printf("%5d|%-5d|%05.1f|%x\n", 42, 42, 3.14159, 255);
    printf("%d\n", (int)(2.99 * 3));
    printf("%.3f\n", (double)1 / 3);
    return 0;
}
