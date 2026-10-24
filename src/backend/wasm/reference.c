/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#include "reference.h"

void reference_address(Wasm *w, const VariantPath *path, size_t target, size_t root, ValueType type,
                       unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = temporary(w), size = 0;
    Diagnostic diagnostic = {0};
    if (!wasm_type_size(type, &size, &diagnostic)) {
        w->code.failed = 1;
    }
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x21, "local.set", top);
    size_t request = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", target);
    indexed(w, 0x20, "local.get", root);
    constant(w, 0, (int64_t)size);
    constant(w, 0, (int64_t)variant_identity(w, type));
    indexed(w, 0x20, "local.get", request);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", ADDRESS_OF_IMPORT);
    indexed(w, 0x20, "local.get", top);
    indexed(w, 0x24, "global.set", 1);
    w->next_local = saved;
}

/* Decode the request service's u32 word from a private result descriptor. */
static void result_word(Wasm *w, size_t result, unsigned offset)
{
    indexed(w, 0x20, "local.get", result);
    memory_op(w, 0x28, "i32.load", 2, offset);
}

void reference_view(Wasm *w, size_t root, size_t size, unsigned line, unsigned column)
{
    size_t target = temporary(w), identity = temporary(w), result = temporary(w);
    indexed(w, 0x21, "local.set", w->wide_local);
    constant(w, 0, 0);
    indexed(w, 0x21, "local.set", identity);
    indexed(w, 0x20, "local.get", w->wide_local);
    op(w, 0xa7, "i32.wrap_i64");
    constant(w, 0, INT32_MIN);
    op(w, 0x71, "i32.and");
    open_control(w, 0x04, "if");
    allocate_storage(w, 48, result, line, column);
    indexed(w, 0x20, "local.get", w->wide_local);
    op(w, 0xa7, "i32.wrap_i64");
    indexed(w, 0x20, "local.get", w->wide_local);
    constant(w, 1, 32);
    op(w, 0x88, "i64.shr_u");
    op(w, 0xa7, "i32.wrap_i64");
    constant(w, 0, (int64_t)size);
    indexed(w, 0x20, "local.get", result);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", ADDRESS_VIEW_IMPORT);
    result_word(w, result, 8);
    open_control(w, 0x04, "if");
    result_word(w, result, 12);
    op(w, 0xad, "i64.extend_i32_u");
    constant(w, 1, 32);
    op(w, 0x86, "i64.shl");
    result_word(w, result, 8);
    op(w, 0xad, "i64.extend_i32_u");
    op(w, 0x84, "i64.or");
    result_word(w, result, 16);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", heap_index(w, HEAP_BORROW));
    op(w, 0x1a, "drop");
    close_control(w);
    result_word(w, result, 0);
    indexed(w, 0x21, "local.set", target);
    result_word(w, result, 20);
    indexed(w, 0x21, "local.set", identity);
    if (root != SIZE_MAX) {
        result_word(w, result, 4);
        indexed(w, 0x21, "local.set", root);
    }
    --w->indent;
    op(w, 0x05, "else");
    ++w->indent;
    indexed(w, 0x20, "local.get", w->wide_local);
    constant(w, 0, (int64_t)size);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", heap_index(w, HEAP_BORROW));
    indexed(w, 0x21, "local.set", target);
    if (root != SIZE_MAX) {
        indexed(w, 0x20, "local.get", target);
        indexed(w, 0x21, "local.set", root);
    }
    close_control(w);
    if (w->variant_path) {
        VariantPath *path = w->variant_path;
        if (path->count == 128) {
            w->code.failed = 1;
        } else {
            path->claims[path->count].arm = NULL;
            path->claims[path->count].field = NULL;
            path->claims[path->count++].address = identity;
        }
    }
    indexed(w, 0x20, "local.get", target);
}

void reference_frame(Wasm *w, int entering)
{
    if (!w->program->has_addresses) {
        return;
    }
    unsigned line = w->routine ? w->routine->line : 1;
    unsigned column = w->routine ? w->routine->column : 1;
    if (entering) {
        indexed(w, 0x23, "global.get", 6);
        constant(w, 0, -1);
        op(w, 0x46, "i32.eq");
        open_control(w, 0x04, "if");
        constant(w, 0, 7);
        constant(w, 0, line);
        constant(w, 0, column);
        indexed(w, 0x10, "call", 6);
        close_control(w);
        indexed(w, 0x23, "global.get", 6);
        constant(w, 0, 1);
        op(w, 0x6a, "i32.add");
        indexed(w, 0x24, "global.set", 6);
        indexed(w, 0x20, "local.get", w->frame_local);
        indexed(w, 0x23, "global.get", 6);
        memory_op(w, 0x36, "i32.store", 2, 12);
    }
    indexed(w, 0x20, "local.get", w->frame_local);
    if (entering) {
        constant(w, 0,
                 w->routine ? (int64_t)w->layout.frames[w->routine->index] : WASM_FRAME_HEADER);
    }
    result_word(w, w->frame_local, 12);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", entering ? FRAME_ENTER_IMPORT : FRAME_LEAVE_IMPORT);
}
