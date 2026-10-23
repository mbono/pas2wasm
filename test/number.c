/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"
#include "ir/number.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Independent known binary64 boundaries and decimal midpoint expectations. */
int main(void)
{
    const struct {
        const char *literal;
        uint64_t bits;
    } cases[] = {
        {"0.0", 0},
        {"0.1", UINT64_C(0x3fb999999999999a)},
        {"1.0", UINT64_C(0x3ff0000000000000)},
        {"9007199254740993.0", UINT64_C(0x4340000000000000)},
        {"1.7976931348623157e308", UINT64_C(0x7fefffffffffffff)},
        {"2.2250738585072014e-308", UINT64_C(0x0010000000000000)},
        {"5e-324", 1},
        {"2.4703282292062328e-324", 1},
        {"1.00000000000000011102230246251565404236316680908203125", UINT64_C(0x3ff0000000000000)},
        {"1.00000000000000011102230246251565404236316680908203126", UINT64_C(0x3ff0000000000001)},
        {"1.00000000000000033306690738754696212708950042724609375", UINT64_C(0x3ff0000000000002)}};
    for (size_t i = 0; i < sizeof(cases) / sizeof(*cases); ++i) {
        uint64_t bits = 0;
        assert(!frontend_real_literal(cases[i].literal, strlen(cases[i].literal), &bits));
        assert(bits == cases[i].bits);
    }
    uint64_t bits;
    const char *invalid[] = {"1e309", "1e-324", "2.4703282292062327e-324"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        assert(strcmp(frontend_real_literal(invalid[i], strlen(invalid[i]), &bits),
                      "E_REAL_RANGE") == 0);
    }
    assert(number_integral(number_bits(2.5), 1) == number_bits(2.0));
    assert(number_integral(number_bits(3.5), 1) == number_bits(4.0));
    assert(number_integral(number_bits(-2.5), 1) == number_bits(-2.0));
    assert(number_integral(number_bits(-3.5), 1) == number_bits(-4.0));
    assert(number_integral(number_bits(-0.5), 1) == (UINT64_C(1) << 63));
    assert(number_integral(number_bits(-1.75), 0) == number_bits(-1.0));
    assert(number_finite(number_bits(1.0)) && !number_finite(UINT64_C(0x7ff0000000000000)));
    puts("Exact decimal rounding, normal/subnormal boundaries, and integral conversions passed.");
    return 0;
}
