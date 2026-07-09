#include <stdio.h>
#include <limits.h>

int main() {
    // sizeof returns bytes; multiplying by CHAR_BIT (usually 8) gives total bits
    printf("Bits in unsigned long: %zu\n", sizeof(unsigned long) * CHAR_BIT);
    return 0;
}