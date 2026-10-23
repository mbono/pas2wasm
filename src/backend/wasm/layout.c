/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "layout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Report invalid/oversized storage at the responsible logical type. */
static int invalid(ValueType type, Diagnostic *diagnostic)
{
    snprintf(diagnostic->code, sizeof(diagnostic->code), "E_LAYOUT");
    snprintf(diagnostic->message, sizeof(diagnostic->message),
             "Invalid, recursive, or oversized program storage layout.");
    diagnostic->line = type ? type->line : 0;
    diagnostic->column = type ? type->column : 0;
    return 0;
}

/* All object sizes are multiples of sixteen; child objects remain 8-byte aligned.
 * A depth/field budget also rejects malformed internal by-value cycles. */
static int object_size(ValueType type, size_t *size, Diagnostic *diagnostic, unsigned depth)
{
    type = type_canonical(type);
    if (!type || depth == 128) {
        return invalid(type, diagnostic);
    }
    if (type->kind == TYPE_KIND_INTEGER || type->kind == TYPE_KIND_BOOLEAN ||
        type->kind == TYPE_KIND_REAL || type->kind == TYPE_KIND_CHARACTER ||
        type->kind == TYPE_KIND_POINTER) {
        *size = WASM_CELL_SIZE;
        return 1;
    }
    uint64_t total = WASM_CELL_SIZE;
    if (type->kind == TYPE_KIND_STRING) {
        if (type->capacity > P2W_STRING_LIMIT) {
            return invalid(type, diagnostic);
        }
        total += (type->capacity + 15) & ~(size_t)15;
    } else if (type->kind == TYPE_KIND_ARRAY) {
        size_t element;
        if (type->upper < type->lower) {
            return invalid(type, diagnostic);
        }
        if (!object_size(type->element, &element, diagnostic, depth + 1)) {
            return 0;
        }
        uint64_t count = (uint64_t)((int64_t)type->upper - type->lower) + 1;
        total += count * element;
    } else if (type->kind == TYPE_KIND_RECORD) {
        size_t fields = 0;
        for (const Field *field = type->fields; field; field = field->next) {
            size_t child;
            if (++fields > 4096) {
                return invalid(type, diagnostic);
            }
            if (!object_size(field->type, &child, diagnostic, depth + 1)) {
                return 0;
            }
            total += child;
            if (total > WASM_MEMORY_LIMIT) {
                return invalid(type, diagnostic);
            }
        }
        if (!fields) {
            return invalid(type, diagnostic);
        }
    } else {
        return invalid(type, diagnostic);
    }
    if (total > WASM_MEMORY_LIMIT) {
        return invalid(type, diagnostic);
    }
    *size = (size_t)total;
    return 1;
}

int wasm_type_size(ValueType type, size_t *size, Diagnostic *diagnostic)
{
    return object_size(type, size, diagnostic, 0);
}

int wasm_layout_build(const Program *program, WasmLayout *layout, Diagnostic *diagnostic)
{
    /* Pointer edges cut physical recursion, but their anonymous targets still
     * need independent finite-size checks even when no allocation is executed. */
    for (const Type *type = program->types; type; type = type->all_next) {
        size_t size;
        if (!wasm_type_size(type, &size, diagnostic)) {
            return 0;
        }
    }
    layout->symbols = calloc(program->symbol_count + 1, sizeof(*layout->symbols));
    layout->frames = calloc(program->routine_count + 1, sizeof(*layout->frames));
    if (!layout->symbols || !layout->frames) {
        snprintf(diagnostic->code, sizeof(diagnostic->code), "E_MEMORY");
        snprintf(diagnostic->message, sizeof(diagnostic->message),
                 "Storage layout allocation failed.");
        return 0;
    }
    size_t globals = P2W_SOURCE_LIMIT;
    for (size_t i = 0; i < program->routine_count; ++i) {
        layout->frames[i] = WASM_FRAME_HEADER;
    }
    for (size_t i = 0; i < program->symbol_count; ++i) {
        const Symbol *symbol = &program->symbols[i];
        if (symbol->kind == SYMBOL_CONSTANT) {
            continue;
        }
        size_t size;
        if (!wasm_type_size(symbol->type, &size, diagnostic)) {
            return 0;
        }
        if (symbol->kind == SYMBOL_TYPE) {
            continue; /* Validate unused named types without reserving any storage. */
        }
        if (symbol->by_reference) {
            size = WASM_CELL_SIZE; /* Private address/root descriptor, not a value copy. */
        }
        size_t *end = symbol->owner == SIZE_MAX ? &globals : &layout->frames[symbol->owner];
        if (*end > WASM_MEMORY_LIMIT - size) {
            return invalid(symbol->type, diagnostic);
        }
        layout->symbols[i] = *end;
        *end += size;
    }
    if (globals > WASM_MEMORY_LIMIT - WASM_FRAME_BUDGET - WASM_CONTROL_BYTES) {
        return invalid(NULL, diagnostic);
    }
    layout->control_base = globals;
    layout->stack_base = globals + WASM_CONTROL_BYTES;
    layout->stack_limit = layout->stack_base + WASM_FRAME_BUDGET;
    return 1;
}

void wasm_layout_destroy(WasmLayout *layout)
{
    free(layout->symbols);
    free(layout->frames);
    memset(layout, 0, sizeof(*layout));
}
