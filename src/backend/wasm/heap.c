/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "emitter.h"
#include <string.h>

/* Private Wasm signatures: l is i64; all ownership stays in program memory. */
const Import heap_functions[HEAP_HELPER_COUNT] = {{"p2w_allocate", "iii", 0x7e},
                                                  {"p2w_validate", "liiii", 0x7f},
                                                  {"p2w_borrow", "liii", 0x7f},
                                                  {"p2w_release", "liii", 0},
                                                  {"p2w_unpin", "i", 0}};

/* Keep local accesses legible in the small, explicitly indexed helper functions. */
static void get(Wasm *w, size_t local)
{
    indexed(w, 0x20, "local.get", local);
}
/* Consume one stack value into a helper-local scratch slot. */
static void set(Wasm *w, size_t local)
{
    indexed(w, 0x21, "local.set", local);
}
/* Read a u32 allocation/borrow field through a checked base local. */
static void load(Wasm *w, size_t base, unsigned offset)
{
    get(w, base);
    memory_op(w, 0x28, "i32.load", 2, offset);
}
/* Store a local into an owned allocation/borrow header field. */
static void store(Wasm *w, size_t base, unsigned offset, size_t value)
{
    get(w, base);
    get(w, value);
    memory_op(w, 0x36, "i32.store", 2, offset);
}
/* Initialize a fixed header field after allocation/growth checks pass. */
static void initialize(Wasm *w, size_t base, unsigned offset, int32_t value)
{
    get(w, base);
    constant(w, 0, value);
    memory_op(w, 0x36, "i32.store", 2, offset);
}
/* Emit a located fail call, selecting disposal diagnostics for release validation. */
static void failed(Wasm *w, unsigned code, size_t line, size_t column, size_t release)
{
    if (release != SIZE_MAX) {
        constant(w, 0, 16);
        constant(w, 0, code);
        get(w, release);
        op(w, 0x1b, "select");
    } else {
        constant(w, 0, code);
    }
    if (line == SIZE_MAX) {
        constant(w, 0, 0);
    } else {
        get(w, line);
    }
    if (column == SIZE_MAX) {
        constant(w, 0, 0);
    } else {
        get(w, column);
    }
    indexed(w, 0x10, "call", 6);
    op(w, 0x00, "unreachable");
}
/* Consume a failure condition without exposing a raw Wasm trap. */
static void require(Wasm *w, unsigned code, size_t line, size_t column, size_t release)
{
    open_control(w, 0x04, "if");
    failed(w, code, line, column, release);
    close_control(w);
}
/* Change branches while retaining the current label depth and readable indentation. */
static void otherwise(Wasm *w)
{
    --w->indent;
    op(w, 0x05, "else");
    ++w->indent;
}
/* Start helper emission with no inherited Pascal loop or lexical activation. */
static void begin(Wasm *w)
{
    w->routine = NULL;
    w->loop = NULL;
    w->depth = 0;
    w->indent = 2;
}
/* Transfer one helper body and its matching WAT, including explicitly typed locals. */
static void finish(Wasm *w, unsigned helper, unsigned locals, Buffer *bodies, Buffer *functions)
{
    const Import *signature = &heap_functions[helper];
    buffer_byte(&w->code, 0x0b);
    Buffer body = {0};
    uleb(&body, locals ? 1 : 0);
    if (locals) {
        uleb(&body, locals);
        buffer_byte(&body, 0x7f);
    }
    buffer_append(&body, w->code.data, w->code.length);
    uleb(bodies, body.length);
    buffer_append(bodies, body.data, body.length);
    if (body.failed || w->code.failed) {
        bodies->failed = 1;
    }
    if (w->emit_wat) {
        buffer_printf(functions, "  (func $%s (param", signature->name);
        for (const char *p = signature->parameters; *p; ++p) {
            buffer_printf(functions, *p == 'l' ? " i64" : " i32");
        }
        buffer_printf(functions, ")");
        if (signature->result) {
            buffer_printf(functions, signature->result == 0x7e ? " (result i64)" : " (result i32)");
        }
        if (locals) {
            buffer_printf(functions, " (local");
            for (unsigned i = 0; i < locals; ++i) {
                buffer_printf(functions, " i32");
            }
            buffer_printf(functions, ")");
        }
        buffer_printf(functions, "\n");
        buffer_append(functions, w->text.data, w->text.length);
        buffer_printf(functions, "  )\n");
        if (w->text.failed) {
            functions->failed = 1;
        }
    }
    buffer_free(&body);
    buffer_free(&w->code);
    buffer_free(&w->text);
}

/* Allocate(size,line,column)->handle. Header locals:3, predecessor:4, cursor:5,
 * end:6, generation:7, needed pages:8, current pages:9, payload:10. */
static void allocation(Wasm *w, Buffer *bodies, Buffer *functions)
{
    begin(w);
    get(w, 0);
    op(w, 0x45, "i32.eqz");
    get(w, 0);
    constant(w, 0, 15);
    op(w, 0x71, "i32.and");
    op(w, 0x72, "i32.or");
    get(w, 0);
    constant(w, 0, WASM_MEMORY_LIMIT - 32);
    op(w, 0x4b, "i32.gt_u");
    op(w, 0x72, "i32.or");
    require(w, 7, 1, 2, SIZE_MAX);
    indexed(w, 0x23, "global.get", 4);
    set(w, 3);
    constant(w, 0, 0);
    set(w, 4);
    open_control(w, 0x02, "block");
    open_control(w, 0x03, "loop");
    get(w, 3);
    op(w, 0x45, "i32.eqz");
    indexed(w, 0x0d, "br_if", 1);
    load(w, 3, 4);
    get(w, 0);
    op(w, 0x4f, "i32.ge_u");
    open_control(w, 0x04, "if");
    load(w, 3, 8);
    constant(w, 0, -1);
    op(w, 0x46, "i32.eq");
    require(w, 7, 1, 2, SIZE_MAX);
    get(w, 4);
    open_control(w, 0x04, "if");
    load(w, 3, 0);
    set(w, 5);
    store(w, 4, 0, 5);
    otherwise(w);
    load(w, 3, 0);
    indexed(w, 0x24, "global.set", 4);
    close_control(w);
    indexed(w, 0x0c, "br", 2);
    close_control(w);
    get(w, 3);
    set(w, 4);
    load(w, 3, 0);
    set(w, 3);
    indexed(w, 0x0c, "br", 0);
    close_control(w);
    close_control(w);
    get(w, 3);
    op(w, 0x45, "i32.eqz");
    open_control(w, 0x04, "if");
    indexed(w, 0x23, "global.get", 3);
    set(w, 3);
    get(w, 3);
    constant(w, 0, WASM_MEMORY_LIMIT - 32);
    get(w, 0);
    op(w, 0x6b, "i32.sub");
    op(w, 0x4b, "i32.gt_u");
    require(w, 7, 1, 2, SIZE_MAX);
    get(w, 3);
    constant(w, 0, 32);
    op(w, 0x6a, "i32.add");
    get(w, 0);
    op(w, 0x6a, "i32.add");
    set(w, 6);
    get(w, 6);
    constant(w, 0, 65535);
    op(w, 0x6a, "i32.add");
    constant(w, 0, 16);
    op(w, 0x76, "i32.shr_u");
    set(w, 8);
    indexed(w, 0x3f, "memory.size", 0);
    set(w, 9);
    get(w, 8);
    get(w, 9);
    op(w, 0x4b, "i32.gt_u");
    open_control(w, 0x04, "if");
    get(w, 8);
    get(w, 9);
    op(w, 0x6b, "i32.sub");
    indexed(w, 0x40, "memory.grow", 0);
    constant(w, 0, -1);
    op(w, 0x46, "i32.eq");
    require(w, 7, 1, 2, SIZE_MAX);
    close_control(w);
    get(w, 6);
    indexed(w, 0x24, "global.set", 3);
    store(w, 3, 4, 0);
    constant(w, 0, 1);
    set(w, 7);
    otherwise(w);
    load(w, 3, 8);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    set(w, 7);
    close_control(w);
    initialize(w, 3, 0, 0);
    store(w, 3, 8, 7);
    initialize(w, 3, 12, 1);
    initialize(w, 3, 16, 0);
    store(w, 3, 20, 0);
    initialize(w, 3, 24, 0);
    initialize(w, 3, 28, 0);
    get(w, 3);
    constant(w, 0, 32);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x22, "local.tee", 10);
    set(w, 5);
    get(w, 10);
    get(w, 0);
    op(w, 0x6a, "i32.add");
    set(w, 6);
    open_control(w, 0x03, "loop");
    initialize(w, 5, 0, 0);
    get(w, 5);
    constant(w, 0, 4);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x22, "local.tee", 5);
    get(w, 6);
    op(w, 0x49, "i32.lt_u");
    indexed(w, 0x0d, "br_if", 0);
    close_control(w);
    get(w, 7);
    op(w, 0xad, "i64.extend_i32_u");
    constant(w, 1, 32);
    op(w, 0x86, "i64.shl");
    get(w, 10);
    op(w, 0xad, "i64.extend_i32_u");
    op(w, 0x84, "i64.or");
    finish(w, HEAP_ALLOCATE, 8, bodies, functions);
}

/* Validate(handle,size,line,column,releaseMode)->payload before any dereference.
 * Release mode maps every invalid-reference case to R_DISPOSE. */
static void validation(Wasm *w, Buffer *bodies, Buffer *functions)
{
    begin(w);
    get(w, 0);
    op(w, 0x50, "i64.eqz");
    require(w, 14, 2, 3, 4);
    get(w, 0);
    op(w, 0xa7, "i32.wrap_i64");
    set(w, 5);
    get(w, 5);
    constant(w, 0, (int64_t)w->layout.stack_limit + 32);
    op(w, 0x49, "i32.lt_u");
    get(w, 5);
    indexed(w, 0x23, "global.get", 3);
    constant(w, 0, 16);
    op(w, 0x6b, "i32.sub");
    op(w, 0x4b, "i32.gt_u");
    op(w, 0x72, "i32.or");
    get(w, 5);
    constant(w, 0, 15);
    op(w, 0x71, "i32.and");
    op(w, 0x72, "i32.or");
    require(w, 18, 2, 3, 4);
    get(w, 5);
    constant(w, 0, 32);
    op(w, 0x6b, "i32.sub");
    set(w, 6);
    get(w, 0);
    constant(w, 1, 32);
    op(w, 0x88, "i64.shr_u");
    op(w, 0xa7, "i32.wrap_i64");
    set(w, 7);
    get(w, 7);
    op(w, 0x45, "i32.eqz");
    require(w, 18, 2, 3, 4);
    load(w, 6, 8);
    get(w, 7);
    op(w, 0x47, "i32.ne");
    load(w, 6, 12);
    constant(w, 0, 1);
    op(w, 0x47, "i32.ne");
    op(w, 0x72, "i32.or");
    require(w, 15, 2, 3, 4);
    load(w, 6, 20);
    get(w, 1);
    op(w, 0x47, "i32.ne");
    load(w, 6, 4);
    get(w, 1);
    op(w, 0x49, "i32.lt_u");
    op(w, 0x72, "i32.or");
    load(w, 6, 4);
    indexed(w, 0x23, "global.get", 3);
    get(w, 5);
    op(w, 0x6b, "i32.sub");
    op(w, 0x4b, "i32.gt_u");
    op(w, 0x72, "i32.or");
    require(w, 18, 2, 3, 4);
    get(w, 5);
    finish(w, HEAP_VALIDATE, 3, bodies, functions);
}

/* Borrow validates a pointee and links a checked lease to the caller's frame. */
static void borrowing(Wasm *w, Buffer *bodies, Buffer *functions)
{
    begin(w);
    get(w, 0);
    get(w, 1);
    get(w, 2);
    get(w, 3);
    constant(w, 0, 0);
    indexed(w, 0x10, "call", heap_index(w, HEAP_VALIDATE));
    set(w, 4);
    get(w, 4);
    constant(w, 0, 32);
    op(w, 0x6b, "i32.sub");
    set(w, 5);
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x22, "local.tee", 6);
    constant(w, 0, (int64_t)w->layout.stack_limit - 16);
    op(w, 0x4b, "i32.gt_u");
    require(w, 7, 2, 3, SIZE_MAX);
    get(w, 6);
    constant(w, 0, 16);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x24, "global.set", 1);
    store(w, 6, 0, 5);
    indexed(w, 0x23, "global.get", 5);
    set(w, 7);
    store(w, 6, 4, 7);
    initialize(w, 6, 8, 0);
    initialize(w, 6, 12, 0);
    get(w, 6);
    indexed(w, 0x24, "global.set", 5);
    load(w, 5, 16);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    set(w, 7);
    store(w, 5, 16, 7);
    get(w, 4);
    finish(w, HEAP_BORROW, 4, bodies, functions);
}

/* Release checks counted controls first, then live borrows, before free-list reuse. */
static void releasing(Wasm *w, Buffer *bodies, Buffer *functions)
{
    begin(w);
    get(w, 0);
    get(w, 1);
    get(w, 2);
    get(w, 3);
    constant(w, 0, 1);
    indexed(w, 0x10, "call", heap_index(w, HEAP_VALIDATE));
    set(w, 4);
    get(w, 4);
    constant(w, 0, 32);
    op(w, 0x6b, "i32.sub");
    set(w, 5);
    constant(w, 0, 0);
    set(w, 6);
    open_control(w, 0x02, "block");
    open_control(w, 0x03, "loop");
    get(w, 6);
    indexed(w, 0x23, "global.get", 2);
    op(w, 0x4f, "i32.ge_u");
    indexed(w, 0x0d, "br_if", 1);
    constant(w, 0, (int64_t)w->layout.control_base);
    get(w, 6);
    constant(w, 0, 4);
    op(w, 0x6c, "i32.mul");
    op(w, 0x6a, "i32.add");
    memory_op(w, 0x28, "i32.load", 2, 0);
    set(w, 7);
    get(w, 7);
    get(w, 4);
    op(w, 0x4f, "i32.ge_u");
    get(w, 7);
    get(w, 4);
    get(w, 1);
    op(w, 0x6a, "i32.add");
    op(w, 0x49, "i32.lt_u");
    op(w, 0x71, "i32.and");
    require(w, 9, 2, 3, SIZE_MAX);
    get(w, 6);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    set(w, 6);
    indexed(w, 0x0c, "br", 0);
    close_control(w);
    close_control(w);
    load(w, 5, 16);
    require(w, 17, 2, 3, SIZE_MAX);
    initialize(w, 5, 12, 0);
    indexed(w, 0x23, "global.get", 4);
    set(w, 7);
    store(w, 5, 0, 7);
    get(w, 5);
    indexed(w, 0x24, "global.set", 4);
    finish(w, HEAP_RELEASE, 4, bodies, functions);
}

/* Unpin(checkpoint) releases only local leases, preserving all caller borrows. */
static void unpinning(Wasm *w, Buffer *bodies, Buffer *functions)
{
    begin(w);
    open_control(w, 0x02, "block");
    open_control(w, 0x03, "loop");
    indexed(w, 0x23, "global.get", 5);
    indexed(w, 0x22, "local.tee", 1);
    get(w, 0);
    op(w, 0x46, "i32.eq");
    indexed(w, 0x0d, "br_if", 1);
    get(w, 1);
    constant(w, 0, (int64_t)w->layout.stack_base);
    op(w, 0x49, "i32.lt_u");
    get(w, 1);
    constant(w, 0, (int64_t)w->layout.stack_limit - 16);
    op(w, 0x4b, "i32.gt_u");
    op(w, 0x72, "i32.or");
    require(w, 18, SIZE_MAX, SIZE_MAX, SIZE_MAX);
    load(w, 1, 0);
    set(w, 2);
    load(w, 1, 4);
    set(w, 3);
    get(w, 2);
    constant(w, 0, (int64_t)w->layout.stack_limit);
    op(w, 0x49, "i32.lt_u");
    get(w, 2);
    indexed(w, 0x23, "global.get", 3);
    constant(w, 0, 48);
    op(w, 0x6b, "i32.sub");
    op(w, 0x4b, "i32.gt_u");
    op(w, 0x72, "i32.or");
    require(w, 18, SIZE_MAX, SIZE_MAX, SIZE_MAX);
    load(w, 2, 16);
    indexed(w, 0x22, "local.tee", 4);
    op(w, 0x45, "i32.eqz");
    require(w, 18, SIZE_MAX, SIZE_MAX, SIZE_MAX);
    get(w, 4);
    constant(w, 0, 1);
    op(w, 0x6b, "i32.sub");
    set(w, 4);
    store(w, 2, 16, 4);
    get(w, 3);
    indexed(w, 0x24, "global.set", 5);
    indexed(w, 0x0c, "br", 0);
    close_control(w);
    close_control(w);
    finish(w, HEAP_UNPIN, 4, bodies, functions);
}

void heap_emit(Wasm *w, Buffer *bodies, Buffer *functions)
{
    allocation(w, bodies, functions);
    validation(w, bodies, functions);
    borrowing(w, bodies, functions);
    releasing(w, bodies, functions);
    unpinning(w, bodies, functions);
}
