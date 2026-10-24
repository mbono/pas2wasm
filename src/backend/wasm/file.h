/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#ifndef P2W_WASM_FILE_H
#define P2W_WASM_FILE_H
#include "variant.h"

/* Appended memory-only services; legacy import signatures/indexes remain unchanged. */
enum {
    FILE_ASSIGN_IMPORT = 39,
    FILE_CONTROL_IMPORT,
    FILE_BEGIN_IMPORT,
    FILE_END_IMPORT,
    FILE_QUERY_IMPORT,
    FILE_WRITE_IMPORT,
    FILE_READ_IMPORT,
    FILE_TEXT_WRITE_IMPORT,
    FILE_TEXT_INTEGER_IMPORT,
    FILE_TEXT_REAL_IMPORT,
    FILE_TEXT_CHAR_IMPORT,
    FILE_TEXT_STRING_IMPORT,
    FILE_TEXT_DISCARD_IMPORT,
    FILE_FINISH_IMPORT
};
/* A grouped input receiver remains borrowed while individual destinations execute.
 * NULL component means text; typed components retain exact declared storage identity. */
typedef struct {
    size_t binding;
    ValueType component;
} FileInput;
/* Assign a captured descriptor without reading its previous initialization state. */
void file_assign(Wasm *w, const VariantPath *path, size_t target, size_t root, ValueType type,
                 size_t filename, unsigned line, unsigned column);
/* Commit one typed item or text remainder with existing control/variant atomicity. */
void file_read(Wasm *w, const VariantPath *path, const FileInput *input, size_t destination,
               ValueType type, int mode, unsigned line, unsigned column);
#endif
