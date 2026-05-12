// Host-side unit test for _int_to_string from modules/ezoec/ezoec.c
//
// The function under test is duplicated here verbatim. Keep in sync with
// the definition in ezoec.c. Build & run with: cc test_int_to_string.c && ./a.out
#include <stdint.h>
#include <stdio.h>
#include <string.h>

char *_int_to_string(uint8_t k, uint8_t precision) {
    enum { buf_len = 12 };
    static char buf[buf_len + 1] = {0};
    char *ptr                    = &buf[buf_len - 1];

    int decimals = 0;
    do {
        *ptr-- = 0x30 + (k % 10);
        k /= 10;
        if (++decimals == precision) {
            *ptr-- = '.';
            if (k == 0)
                *ptr-- = '0';
        }
    } while (k > 0 || decimals < precision);

    return ptr + 1;
}

#define CHECK(k, prec, expected)                                                                   \
    do {                                                                                           \
        const char *got = _int_to_string((k), (prec));                                             \
        if (strcmp(got, (expected)) != 0) {                                                        \
            fprintf(stderr, "FAIL: _int_to_string(%u, %u) -> \"%s\", expected \"%s\"\n",           \
                    (unsigned)(k), (unsigned)(prec), got, (expected));                             \
            failures++;                                                                            \
        }                                                                                          \
    } while (0)

int main(void) {
    int failures = 0;

    // Bug fix: k == 0 used to return an empty string and made ezoec_set_k
    // send "K,\r" which the EZO rejects with *ER.
    CHECK(0, 0, "0");
    CHECK(0, 1, "0.0");
    CHECK(0, 2, "0.00");
    CHECK(0, 3, "0.000");

    // No decimals.
    CHECK(1, 0, "1");
    CHECK(9, 0, "9");
    CHECK(10, 0, "10");
    CHECK(123, 0, "123");
    CHECK(255, 0, "255");

    // 1 decimal (K-value scaling: stored value / 10).
    CHECK(1, 1, "0.1");
    CHECK(10, 1, "1.0");
    CHECK(15, 1, "1.5");
    CHECK(100, 1, "10.0");
    CHECK(102, 1, "10.2"); // EZO max K
    CHECK(255, 1, "25.5");

    // 2 decimals — leading zero padding when k < 10^(precision-1).
    CHECK(1, 2, "0.01");
    CHECK(9, 2, "0.09");
    CHECK(10, 2, "0.10");
    CHECK(99, 2, "0.99");
    CHECK(100, 2, "1.00");
    CHECK(255, 2, "2.55");

    // 3 decimals.
    CHECK(1, 3, "0.001");
    CHECK(99, 3, "0.099");
    CHECK(100, 3, "0.100");
    CHECK(255, 3, "0.255");

    if (failures == 0) {
        puts("OK: all _int_to_string cases passed");
        return 0;
    }
    fprintf(stderr, "%d failure(s)\n", failures);
    return 1;
}
