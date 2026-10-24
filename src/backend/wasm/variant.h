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
        const VariantArm *arm; /* NULL denotes an address-reference claim group. */
        const Field *field;    /* NULL requires the exact ancestor arm. */
        size_t address;
    } claims[128];
} VariantPath;

enum { VARIANT_ACCESS_IMPORT = 30, VARIANT_COPY_IMPORT, VARIANT_READ_IMPORT };
enum {
    ADDRESS_OF_IMPORT = 33,
    ADDRESS_VIEW_IMPORT,
    FRAME_ENTER_IMPORT,
    FRAME_LEAVE_IMPORT,
    VARIANT_BIND_IMPORT
};
enum { VARIANT_CHAR_STORE_IMPORT = 38 };
/* Backend identities and requests remain opaque to the frontend and Pascal code. */
size_t variant_identity(Wasm *w, ValueType type);
size_t variant_request(Wasm *w, const VariantPath *path, unsigned line, unsigned column);
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
/* Preflight character cells/views and activation together before committing either. */
void variant_store_character(Wasm *w, const VariantPath *path, size_t destination, size_t value,
                             unsigned line, unsigned column);
/* True when a value copy can overwrite variant headers (pointer edges are excluded). */
int variant_value(ValueType type);
/* Release backend-only immutable schema/type-identity caches after module generation. */
void variant_destroy(Wasm *w);
#endif
