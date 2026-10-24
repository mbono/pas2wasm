/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_WASM_EMITTER_H
#define P2W_WASM_EMITTER_H
#include "backend/backend.h"
#include "layout.h"

/* Lowering owns buffers and a backend storage map. Values/flags are addressable
 * in bounded linear memory; scratch locals are assigned only after evaluation.
 * next_local/local_count track active bound locals and their allocation high-water mark. */
typedef struct {
    Buffer code, data, text;
    int emit_wat, heap, enum_data_overflow;
    unsigned indent;
    unsigned depth;
    const struct Loop *loop;
    const struct WithContext *with_scope;
    const Program *program;
    const Routine *routine;
    WasmLayout layout;
    struct EnumTable *enum_tables; /* Backend-owned member-name tables, one per enum type. */
    size_t frame_local, left_local, right_local, wide_local, real_local;
    size_t next_local, local_count;
} Wasm;
/* Function imports are fixed by ABI pas2wasm_v1; indexes also address call ops. */
typedef struct {
    const char *name, *parameters; /* i = i32, l = i64, d = f64; one character per parameter. */
    unsigned result;               /* Zero denotes a void result. */
} Import;

enum { HEAP_ALLOCATE, HEAP_VALIDATE, HEAP_BORROW, HEAP_RELEASE, HEAP_UNPIN, HEAP_HELPER_COUNT };
extern const Import heap_functions[HEAP_HELPER_COUNT];
/* Private helper indexes follow the Pascal functions; no extra production exports. */
size_t heap_index(const Wasm *w, unsigned helper);
/* Shared direct binary/WAT primitives keep heap and language lowering identical. */
void uleb(Buffer *buffer, uint64_t value);
void op(Wasm *w, unsigned opcode, const char *text);
void indexed(Wasm *w, unsigned opcode, const char *text, size_t index);
void constant(Wasm *w, int wide, int64_t value);
void open_control(Wasm *w, unsigned opcode, const char *text);
void close_control(Wasm *w);
void memory_op(Wasm *w, unsigned opcode, const char *text, unsigned align, unsigned offset);
/* Append repository-owned allocation/lifetime helpers, using only program memory. */
void heap_emit(Wasm *w, Buffer *bodies, Buffer *functions);
#endif
