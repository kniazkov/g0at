/** @file integer_oracle_testing.c
 * @copyright 2026 Ivan Kniazkov
 * @brief Independent Python parity and negative checks for the exact test oracle.
 */
#include "test/support/integer_oracle.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int equal(integer_oracle_value_t actual,
                 int64_t clamped,
                 integer_oracle_value_t expected,
                 int64_t expected_clamped) {
    if (actual.negative != expected.negative || clamped != expected_clamped)
        return 0;
    for (int i = 0; i < 4; ++i)
        if (actual.limb[i] != expected.limb[i])
            return 0;
    return 1;
}

/* Exercise the full public 128-bit clamp range, not just reachable int64 products. */
static int clamp_boundaries(void) {
    static const struct {
        integer_oracle_value_t value;
        int64_t expected;
    } cases[] = {
        {{false, {0, 0, 0, 0}}, 0},
        {{true, {0, 0, 0, 0}}, 0},
        {{false, {UINT32_MAX, UINT32_C(0x7fffffff), 0, 0}}, INT64_MAX},
        {{true, {UINT32_MAX, UINT32_C(0x7fffffff), 0, 0}}, -INT64_MAX},
        {{false, {0, UINT32_C(0x80000000), 0, 0}}, INT64_MAX},
        {{true, {0, UINT32_C(0x80000000), 0, 0}}, INT64_MIN},
        {{true, {1, UINT32_C(0x80000000), 0, 0}}, INT64_MIN},
        {{false, {0, 0, 1, 0}}, INT64_MAX},
        {{true, {0, 0, 1, 0}}, INT64_MIN},
        {{false, {0, 0, 0, 1}}, INT64_MAX},
        {{true, {0, 0, 0, 1}}, INT64_MIN},
        {{false, {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}}, INT64_MAX},
        {{true, {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}}, INT64_MIN},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        if (integer_oracle_saturate(cases[i].value) != cases[i].expected) {
            fprintf(stderr, "clamp boundary %zu failed\n", i);
            return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr,
                "usage: %s parity|reject-value|reject-sign|reject-clamp fixture\n",
                argv[0]);
        return 1;
    }
    int mutation = 0;
    if (strcmp(argv[1], "reject-value") == 0)
        mutation = 1;
    else if (strcmp(argv[1], "reject-sign") == 0)
        mutation = 2;
    else if (strcmp(argv[1], "reject-clamp") == 0)
        mutation = 3;
    else if (strcmp(argv[1], "parity") != 0) {
        fprintf(stderr, "unknown scenario: %s\n", argv[1]);
        return 1;
    }
    if (!clamp_boundaries())
        return 1;
    FILE *input = fopen(argv[2], "r");
    if (!input) {
        perror(argv[2]);
        return 1;
    }
    size_t count = 0;
    int result = 1;
    for (;;) {
        unsigned operation, negative;
        int64_t a, b, expected_clamped;
        integer_oracle_value_t expected;
        int fields = fscanf(input,
                            "%u %" SCNd64 " %" SCNd64 " %u %" SCNu32 " %" SCNu32 " %" SCNu32
                            " %" SCNu32 " %" SCNd64,
                            &operation,
                            &a,
                            &b,
                            &negative,
                            &expected.limb[0],
                            &expected.limb[1],
                            &expected.limb[2],
                            &expected.limb[3],
                            &expected_clamped);
        if (fields == EOF && !ferror(input))
            break;
        if (fields != 9 || operation != count % 7 || negative > 1 || count >= 8372) {
            fprintf(stderr, "invalid fixture record %zu (%d fields)\n", count + 1, fields);
            goto done;
        }
        expected.negative = negative != 0;
        integer_oracle_value_t actual =
            integer_oracle_evaluate((integer_oracle_operation_t)operation, a, b);
        int64_t clamped = integer_oracle_saturate(actual);
        if (!equal(actual, clamped, expected, expected_clamped)) {
            fprintf(stderr,
                    "Python parity mismatch at record %zu, operation %u\n",
                    count + 1,
                    operation);
            goto done;
        }
        if (mutation == 1)
            actual.limb[count % 4] ^= UINT32_C(1);
        else if (mutation == 2)
            actual.negative = !actual.negative;
        else if (mutation == 3)
            clamped = clamped == INT64_MAX ? INT64_MIN : INT64_MAX;
        if (mutation && equal(actual, clamped, expected, expected_clamped)) {
            fprintf(stderr, "checker accepted mutation at record %zu\n", count + 1);
            goto done;
        }
        ++count;
    }
    if (count != 8372) {
        fprintf(stderr, "expected 8372 records, read %zu\n", count);
        goto done;
    }
    printf("%s: %zu Python exact and saturated results verified\n", argv[1], count);
    result = 0;
done:
    if (fclose(input) != 0) {
        perror("close fixture");
        result = 1;
    }
    return result;
}
