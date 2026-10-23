/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"
#include <string.h>

#define BIG_WORDS 256

/* Fixed-capacity unsigned limbs. Literal limits bound every intermediate below
 * 8,192 bits; all growth is checked, with no allocator/locale/fenv dependency. */
typedef struct {
    uint32_t words[BIG_WORDS];
    size_t count;
} Big;

/* Multiply by ten and add one decimal digit, checking the final carry. */
static int decimal_digit(Big *value, unsigned digit)
{
    uint64_t carry = digit;
    for (size_t i = 0; i < value->count; ++i) {
        uint64_t product = (uint64_t)value->words[i] * 10 + carry;
        value->words[i] = (uint32_t)product;
        carry = product >> 32;
    }
    if (carry) {
        if (value->count == BIG_WORDS) {
            return 0;
        }
        value->words[value->count++] = (uint32_t)carry;
    }
    return 1;
}

/* Compare canonical unsigned limb sequences. */
static int compare(const Big *a, const Big *b)
{
    if (a->count != b->count) {
        return a->count > b->count ? 1 : -1;
    }
    for (size_t i = a->count; i > 0; --i) {
        if (a->words[i - 1] != b->words[i - 1]) {
            return a->words[i - 1] > b->words[i - 1] ? 1 : -1;
        }
    }
    return 0;
}

/* Shift left with bounded growth; unused low words are always zero. */
static int shift_left(Big *value, unsigned bits)
{
    if (!value->count || !bits) {
        return 1;
    }
    size_t whole = bits / 32;
    unsigned part = bits % 32;
    if (value->count + whole + 1 > BIG_WORDS) {
        return 0;
    }
    memmove(value->words + whole, value->words, value->count * sizeof(uint32_t));
    memset(value->words, 0, whole * sizeof(uint32_t));
    value->count += whole;
    uint64_t carry = 0;
    for (size_t i = whole; i < value->count; ++i) {
        uint64_t word = ((uint64_t)value->words[i] << part) | carry;
        value->words[i] = (uint32_t)word;
        carry = word >> 32;
    }
    if (carry) {
        value->words[value->count++] = (uint32_t)carry;
    }
    return 1;
}

/* Subtract b <= a without signed arithmetic or borrow overflow. */
static void subtract(Big *a, const Big *b)
{
    uint64_t borrow = 0;
    for (size_t i = 0; i < a->count; ++i) {
        uint64_t right = (i < b->count ? b->words[i] : 0) + borrow;
        uint64_t left = a->words[i];
        a->words[i] = (uint32_t)(left - right);
        borrow = left < right;
    }
    while (a->count && !a->words[a->count - 1]) {
        --a->count;
    }
}

/* Return the exact binary length of a nonnegative integer. */
static int bit_length(const Big *value)
{
    if (!value->count) {
        return 0;
    }
    uint32_t top = value->words[value->count - 1];
    int bits = (int)(value->count - 1) * 32;
    while (top) {
        ++bits;
        top >>= 1;
    }
    return bits;
}

/* Round a positive rational to IEEE binary64, including subnormal boundaries.
 * Only a 53-bit quotient is needed; the exact remainder decides ties-to-even. */
static const char *rational_bits(Big numerator, Big denominator, uint64_t *bits)
{
    int exponent = bit_length(&numerator) - bit_length(&denominator);
    Big scaled = exponent >= 0 ? denominator : numerator;
    if (!shift_left(&scaled, (unsigned)(exponent >= 0 ? exponent : -exponent))) {
        return "E_LIMIT";
    }
    if ((exponent >= 0 ? compare(&numerator, &scaled) : compare(&scaled, &denominator)) < 0) {
        --exponent;
    }
    int scale = exponent < -1022 ? 1074 : 52 - exponent;
    if (!shift_left(scale >= 0 ? &numerator : &denominator,
                    (unsigned)(scale >= 0 ? scale : -scale))) {
        return "E_LIMIT";
    }
    uint64_t quotient = 0;
    for (int bit = 53; bit >= 0; --bit) {
        scaled = denominator;
        if (!shift_left(&scaled, (unsigned)bit)) {
            return "E_LIMIT";
        }
        if (compare(&numerator, &scaled) >= 0) {
            subtract(&numerator, &scaled);
            quotient |= UINT64_C(1) << bit;
        }
    }
    if (!shift_left(&numerator, 1)) {
        return "E_LIMIT";
    }
    int half = compare(&numerator, &denominator);
    if (half > 0 || (half == 0 && (quotient & 1))) {
        ++quotient;
    }
    if (!quotient) {
        return "E_REAL_RANGE";
    }
    if (exponent < -1022) {
        *bits = quotient; /* Rounding to 2^52 naturally becomes the smallest normal. */
        return NULL;
    }
    if (quotient == (UINT64_C(1) << 53)) {
        quotient >>= 1;
        ++exponent;
    }
    if (exponent > 1023) {
        return "E_REAL_RANGE";
    }
    *bits = ((uint64_t)(exponent + 1023) << 52) | (quotient & ((UINT64_C(1) << 52) - 1));
    return NULL;
}

const char *frontend_real_literal(const char *text, size_t length, uint64_t *bits)
{
    Big numerator = {0}, denominator = {.words = {1}, .count = 1};
    size_t offset = 0;
    int fraction = 0, after_point = 0, digits = 0, significant = 0;
    while (offset < length && text[offset] != 'e' && text[offset] != 'E') {
        unsigned c = (unsigned char)text[offset++];
        if (c == '.' && !after_point) {
            after_point = 1;
            continue;
        }
        if (c < '0' || c > '9') {
            return "E_REAL_LITERAL";
        }
        if (++digits > 768 || !decimal_digit(&numerator, c - '0')) {
            return "E_LIMIT";
        }
        fraction += after_point;
        if (c != '0' || significant) {
            ++significant;
        }
    }
    if (!digits) {
        return "E_REAL_LITERAL";
    }
    int exponent = 0, negative = 0;
    if (offset < length) {
        ++offset;
        if (offset < length && (text[offset] == '+' || text[offset] == '-')) {
            negative = text[offset++] == '-';
        }
        if (offset == length) {
            return "E_REAL_LITERAL";
        }
        while (offset < length) {
            unsigned c = (unsigned char)text[offset++];
            if (c < '0' || c > '9') {
                return "E_REAL_LITERAL";
            }
            exponent = exponent * 10 + (int)c - '0';
            if (exponent > 4096) {
                return "E_LIMIT";
            }
        }
    }
    if (!numerator.count) {
        *bits = 0;
        return NULL;
    }
    exponent = (negative ? -exponent : exponent) - fraction;
    int magnitude = significant + exponent - 1;
    if (magnitude < -324 || magnitude > 308) {
        return "E_REAL_RANGE";
    }
    for (int i = 0; i < (exponent >= 0 ? exponent : -exponent); ++i) {
        if (!decimal_digit(exponent >= 0 ? &numerator : &denominator, 0)) {
            return "E_LIMIT";
        }
    }
    return rational_bits(numerator, denominator, bits);
}
