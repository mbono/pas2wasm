/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#ifndef P2W_WASM_REFERENCE_H
#define P2W_WASM_REFERENCE_H
#include "variant.h"

/* Produce an opaque reference to already captured storage, preserving its root/type. */
void reference_address(Wasm *w, const VariantPath *path, size_t target, size_t root, ValueType type,
                       unsigned line, unsigned column);
/* Consume a pointer handle, borrow its owner, and push the actual typed address.
 * Dynamic view claims join the current capture; root may be SIZE_MAX for reads. */
void reference_view(Wasm *w, size_t root, size_t size, unsigned line, unsigned column);
/* Register/retire the current Pascal activation using its checked private epoch. */
void reference_frame(Wasm *w, int entering);
#endif
