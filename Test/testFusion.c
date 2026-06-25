#include <stdio.h>

int main() {\

    int n = 10;
    int a[10] = {1, -2, 3, -4, 5, -6, 7, -8, 9, -10};
    int b[10] = {10, -9, 8, -7, 6, -5, 4, -3, 2, -1};

    for (int i = 0; i < n; i++) {
        if (a[i] > 0) {
            a[i] = a[i] + 1;
        } else {
            a[i] = a[i] - 1;
        }
    }

    for (int j = 0; j < n; j++) {
        if (b[j] > 0) {
            b[j] = b[j] * 2;
        } else {
            b[j] = b[j] * 3;
        }
    
}

    return 0;
}