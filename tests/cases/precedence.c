#include <stdio.h>
int main() {
    int a = 2, b = 3, c = 4;
    printf("%d %d %d %d\n", a + b * c, (a + b) * c, a - b - c, a * b % c);
    printf("%d %d\n", 10 - 4 - 3, 100 / 10 / 5);
    printf("%d %d %d\n", a < b < c, c > b > a, a == 2 == 1);
    printf("%d %d\n", -a * -b, !a + !0);
    printf("%d\n", a > 1 ? b > 5 ? 1 : 2 : 3);
    printf("%d %d\n", a || b && 0, (a || b) && 0);
    int x = 5;
    int y = (x = 3) + 1;
    printf("%d %d\n", x, y);
    return 0;
}
