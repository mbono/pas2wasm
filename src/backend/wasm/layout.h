/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_WASM_LAYOUT_H
#define P2W_WASM_LAYOUT_H
#include "ir/program.h"

#define WASM_MEMORY_LIMIT (64u * 1024u * 1024u)
#define WASM_FRAME_BUDGET (128u * 1024u)
#define WASM_CONTROL_LIMIT (P2W_CALL_DEPTH_LIMIT * 128u)
#define WASM_CONTROL_BYTES (WASM_CONTROL_LIMIT * 4u)
#define WASM_CELL_SIZE 16u
#define WASM_FRAME_HEADER 16u

/* Backend-owned offsets and frame sizes; destroy even after construction fails.
 * Frontend types/symbols remain immutable and contain no concrete storage data. */
typedef struct {
    size_t *symbols, *frames;
    size_t control_base, stack_base, stack_limit;
} WasmLayout;

/* Compute an aligned object size with bounded, checked compound traversal. */
int wasm_type_size(ValueType type, size_t *size, Diagnostic *diagnostic);
/* Lay out globals and per-routine frames inside the bounded program memory. */
int wasm_layout_build(const Program *program, WasmLayout *layout, Diagnostic *diagnostic);
/* Release all backend-owned mapping arrays and reset the layout. */
void wasm_layout_destroy(WasmLayout *layout);
#endif
