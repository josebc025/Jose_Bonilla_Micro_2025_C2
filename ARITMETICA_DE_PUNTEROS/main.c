#include <stdio.h>
#include <stdlib.h>

int main() {
    // int a = 3;
    // int *p = &a;
    // printf("The address of a is %p\n", &a);
    // printf("The address of a from p is %p\n", p);
    // printf("The value of a from p is %i\n", *p); // 3

    // printf("The value of the variable pointed by p is %i\n", *p);
    // printf("The value of p %p\n", p);

    char c[3] = { 'A', 'B', 'C' };
    char *p2 = c;

    for (int i = 0; i < 3; i++) {
        printf("Memory address of c[%i] = %p\n", i, p2 + i);
        printf("Value of c[%i] = %c\n", i, *(p2 + i));
    }

    return 0;
}
