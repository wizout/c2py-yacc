#include <stdio.h>

/* масиви передаються у функції "за посиланням" */

void print_array(int a[], int n) {
    for (int i = 0; i < n; i++)
        printf("%d%s", a[i], i < n - 1 ? ", " : "\n");
}

void bubble_sort(int a[], int n) {
    int swapped;
    do {
        swapped = 0;
        for (int i = 0; i + 1 < n; i++) {
            if (a[i] > a[i + 1]) {
                int t = a[i];
                a[i] = a[i + 1];
                a[i + 1] = t;
                swapped = 1;
            }
        }
        n--;
    } while (swapped);
}

int binary_search(int a[], int n, int x) {
    int lo = 0, hi = n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (a[mid] == x) return mid;
        else if (a[mid] < x) lo = mid + 1;
        else hi = mid - 1;
    }
    return -1;
}

int main() {
    int a[10] = {42, 7, -3, 19, 0, 88, 7, 15, -20, 61};
    int n = 10;

    printf("before: ");
    print_array(a, n);
    bubble_sort(a, n);
    printf("after:  ");
    print_array(a, n);

    printf("index of 19: %d\n", binary_search(a, n, 19));
    printf("index of 5:  %d\n", binary_search(a, n, 5));
    return 0;
}
