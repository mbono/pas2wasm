/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#include "file.h"

/* Layout has validated declared types; all physical sizes remain backend-owned. */
static size_t object_size(Wasm *w, ValueType type)
{
    size_t size = 0;
    Diagnostic diagnostic = {0};
    if (!wasm_type_size(type, &size, &diagnostic)) {
        w->code.failed = 1;
    }
    return size;
}

void file_assign(Wasm *w, const VariantPath *path, size_t target, size_t root, ValueType type,
                 size_t filename, unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = temporary(w);
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x21, "local.set", top);
    size_t request = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", target);
    indexed(w, 0x20, "local.get", root);
    constant(w, 0, type == TYPE_TEXT);
    constant(w, 0, type == TYPE_TEXT ? 0 : (int64_t)variant_identity(w, type->element));
    constant(w, 0, type == TYPE_TEXT ? 0 : (int64_t)object_size(w, type->element));
    indexed(w, 0x20, "local.get", filename);
    indexed(w, 0x20, "local.get", request);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", FILE_ASSIGN_IMPORT);
    indexed(w, 0x20, "local.get", top);
    indexed(w, 0x24, "global.set", 1);
    w->next_local = saved;
}

void file_read(Wasm *w, const VariantPath *path, const FileInput *input, size_t destination,
               ValueType type, int mode, unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = temporary(w);
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x21, "local.set", top);
    size_t request = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", input->binding);
    indexed(w, 0x20, "local.get", destination);
    constant(w, 0, input->component ? (int64_t)object_size(w, type) : (int64_t)type->capacity);
    if (!input->component) {
        constant(w, 0, mode);
    }
    indexed(w, 0x20, "local.get", request);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", input->component ? FILE_READ_IMPORT : FILE_TEXT_STRING_IMPORT);
    indexed(w, 0x20, "local.get", top);
    indexed(w, 0x24, "global.set", 1);
    w->next_local = saved;
}
