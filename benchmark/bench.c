/* benchmark/bench.c  — sum loop 0..20,000,000 */
#include <stdio.h>
int main(void) {
    long n = 20000000, i = 0, sum = 0;
    while (i < n) { sum += i; i++; }
    printf("%ld\n", sum);
    return 0;
}
