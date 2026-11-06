#include <stdio.h>
int moves = 0;
void hanoi(int n, int from, int to, int via) {
    if (n == 0) return;
    hanoi(n - 1, from, via, to);
    moves++;
    hanoi(n - 1, via, to, from);
}
void quicksort(int a[], int lo, int hi) {
    if (lo >= hi) return;
    int p = a[(lo + hi) / 2], i = lo, j = hi;
    while (i <= j) {
        while (a[i] < p) i++;
        while (a[j] > p) j--;
        if (i <= j) {
            int t = a[i]; a[i] = a[j]; a[j] = t;
            i++; j--;
        }
    }
    quicksort(a, lo, j);
    quicksort(a, i, hi);
}
int ackermann(int m, int n) {
    if (m == 0) return n + 1;
    if (n == 0) return ackermann(m - 1, 1);
    return ackermann(m - 1, ackermann(m, n - 1));
}
int is_even(int n);
int is_odd(int n) { return n == 0 ? 0 : is_even(n - 1); }
int is_even(int n) { return n == 0 ? 1 : is_odd(n - 1); }
int main() {
    hanoi(10, 1, 3, 2);
    printf("hanoi(10): %d moves\n", moves);
    int a[12] = {5, -1, 9, 3, 3, 0, 12, -8, 7, 1, 2, 6};
    quicksort(a, 0, 11);
    for (int i = 0; i < 12; i++) printf("%d ", a[i]);
    printf("\nack(2,3) = %d\n", ackermann(2, 3));
    printf("%d %d\n", is_even(10), is_odd(7));
    return 0;
}
