/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#include "crt.h"

void crt_emit(Wasm *w, Buffer *bodies, Buffer *functions)
{
    w->routine = NULL;
    w->loop = NULL;
    w->depth = 0;
    w->indent = 2;
    indexed(w, 0x20, "local.get", 0);
    indexed(w, 0x20, "local.get", 1);
    indexed(w, 0x10, "call", CRT_KEY_IMPORT);
    indexed(w, 0x22, "local.tee", 2);
    constant(w, 0, 0x10ffff);
    op(w, 0x4b, "i32.gt_u"); /* Also rejects negative imported i32 values. */
    indexed(w, 0x20, "local.get", 2);
    constant(w, 0, 0xd800);
    op(w, 0x4f, "i32.ge_u");
    indexed(w, 0x20, "local.get", 2);
    constant(w, 0, 0xdfff);
    op(w, 0x4d, "i32.le_u");
    op(w, 0x71, "i32.and");
    op(w, 0x72, "i32.or");
    open_control(w, 0x04, "if");
    constant(w, 0, 13);
    indexed(w, 0x20, "local.get", 0);
    indexed(w, 0x20, "local.get", 1);
    indexed(w, 0x10, "call", 6);
    close_control(w);
    indexed(w, 0x20, "local.get", 2);
    buffer_byte(&w->code, 0x0b);
    Buffer body = {0};
    uleb(&body, 1);
    uleb(&body, 1);
    buffer_byte(&body, 0x7f);
    buffer_append(&body, w->code.data, w->code.length);
    uleb(bodies, body.length);
    buffer_append(bodies, body.data, body.length);
    if (body.failed || w->code.failed) {
        bodies->failed = 1;
    }
    if (w->emit_wat) {
        buffer_printf(functions, "  (func $p2w_readkey (param i32 i32) (result i32) (local i32)\n");
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
