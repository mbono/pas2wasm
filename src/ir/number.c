/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "number.h"
#include <float.h>
#include <string.h>

_Static_assert(sizeof(double) == 8 && DBL_MANT_DIG == 53 && DBL_MAX_EXP == 1024 && FLT_RADIX == 2,
               "Compiler hosts require IEEE binary64 double.");

uint64_t number_bits(double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

double number_value(uint64_t bits)
{
    double value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

int number_finite(uint64_t bits)
{
    return ((bits >> 52) & 2047) != 2047;
}

uint64_t number_integral(uint64_t bits, int nearest)
{
    uint64_t sign = bits & (UINT64_C(1) << 63);
    uint64_t magnitude = bits & ~(UINT64_C(1) << 63);
    int exponent = (int)(magnitude >> 52) - 1023;
    if (exponent >= 52) {
        return bits;
    }
    if (exponent < 0) {
        if (nearest && magnitude > UINT64_C(0x3fe0000000000000)) {
            return sign | UINT64_C(0x3ff0000000000000);
        }
        return sign;
    }
    unsigned shift = 52u - (unsigned)exponent;
    uint64_t mask = (UINT64_C(1) << shift) - 1;
    uint64_t fraction = magnitude & mask;
    magnitude &= ~mask;
    uint64_t half = UINT64_C(1) << (shift - 1);
    if (nearest && (fraction > half || (fraction == half && ((magnitude >> shift) & 1)))) {
        magnitude += UINT64_C(1) << shift;
    }
    return sign | magnitude;
}
