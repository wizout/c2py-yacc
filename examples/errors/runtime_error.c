#include <stdio.h>

int main() {
    int a[5] = {1, 2, 3, 4, 5};
    int sum = 0;
    for (int i = 0; i <= 5; i++) {   /* помилка: i = 5 виходить за межі */
        printf("a[%d] = %d\n", i, a[i]);
        sum += a[i];
    }
    printf("sum = %d\n", sum);
    return 0;
}
