/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_NUMBER_H
#define P2W_NUMBER_H
#include <stdint.h>

/* Binary64 helpers carry logical numeric bits, never backend instructions/layout. */
uint64_t number_bits(double value);
/* Recover the binary64 value from its exact, host-endian-independent integer bits. */
double number_value(uint64_t bits);
/* Test finite values without relying on floating-point exception/NaN modes. */
int number_finite(uint64_t bits);
/* Truncate or round nearest/ties-even using significand bits, including signed zero. */
uint64_t number_integral(uint64_t bits, int nearest);
#endif
