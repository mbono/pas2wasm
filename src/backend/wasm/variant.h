/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_WASM_VARIANT_H
#define P2W_WASM_VARIANT_H
#include "emitter.h"

/* A captured selector retains logical claims and runtime part addresses. The
 * frontend's combined path-depth limit bounds this backend stack object. */
typedef struct VariantPath {
    size_t count;
    struct {
        const VariantArm *arm;
        const Field *field; /* NULL requires the exact ancestor arm. */
        size_t address;
    } claims[128];
} VariantPath;

enum { VARIANT_ACCESS_IMPORT = 30, VARIANT_COPY_IMPORT, VARIANT_READ_IMPORT };
/* Append a field's ancestor claims, relative to its captured outer record base. */
void variant_capture(Wasm *w, VariantPath *path, const Field *field, size_t base);
/* pin == 0 validates reads; -1 activates a validated scalar commit; +1 activates
 * and pins a writable binding. Claims and request storage remain caller-owned. */
void variant_access(Wasm *w, const VariantPath *path, int pin, unsigned line, unsigned column);
/* Commit a validated source atomically; size zero denotes a string of given capacity. */
void variant_copy(Wasm *w, const VariantPath *path, size_t destination, size_t source, size_t size,
                  size_t capacity, unsigned line, unsigned column);
/* Read/validate a string before activating destination views; preserves JSPI waits. */
void variant_read_string(Wasm *w, const VariantPath *path, size_t destination, size_t capacity,
                         int input_line, unsigned line, unsigned column);
/* True when a value copy can overwrite variant headers (pointer edges are excluded). */
int variant_value(ValueType type);
/* Release backend-only immutable schema/type-identity caches after module generation. */
void variant_destroy(Wasm *w);
#endif
