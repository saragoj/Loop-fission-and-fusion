#include <stdio.h>

int main() {

    int n = 10;
    int a[10];
    int b[10];

    for (int i = 0; i < n; i++) {
        a[i] = i + 1;
    }

    for (int i = 0; i < n; i++) {
        b[i] = i * 2;
    }

    return 0;
}