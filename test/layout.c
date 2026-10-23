/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "backend/wasm/layout.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Check backend layout contracts independently of parser syntax and emission. */
int main(void)
{
    Diagnostic diagnostic = {0};
    size_t size = 0;
    assert(wasm_type_size(TYPE_INTEGER, &size, &diagnostic) && size == 16);
    assert(wasm_type_size(TYPE_CHARACTER, &size, &diagnostic) && size == 16);
    Type pointer = {.kind = TYPE_KIND_POINTER, .element = TYPE_INTEGER};
    assert(wasm_type_size(&pointer, &size, &diagnostic) && size == 16);
    Type text = {.kind = TYPE_KIND_STRING, .capacity = 17};
    assert(wasm_type_size(&text, &size, &diagnostic) && size == 48);
    text.capacity = 0;
    assert(wasm_type_size(&text, &size, &diagnostic) && size == 16);
    text.capacity = P2W_STRING_LIMIT;
    assert(wasm_type_size(&text, &size, &diagnostic) && size == P2W_STRING_LIMIT + 16);
    text.capacity++;
    assert(!wasm_type_size(&text, &size, &diagnostic));
    Type flags = {.kind = TYPE_KIND_ARRAY, .element = TYPE_BOOLEAN, .lower = -1, .upper = 1};
    Field array_field = {.name = "b", .type = &flags};
    Field integer_field = {.name = "a", .type = TYPE_INTEGER, .next = &array_field};
    Type record = {.kind = TYPE_KIND_RECORD, .fields = &integer_field};
    assert(wasm_type_size(&record, &size, &diagnostic) && size == 96);
    Field boolean_field = {.name = "b", .type = TYPE_BOOLEAN};
    integer_field.next = &boolean_field;
    Type rows = {.kind = TYPE_KIND_ARRAY, .element = &record, .lower = 1, .upper = 2};
    assert(wasm_type_size(&rows, &size, &diagnostic) && size == 112);
    Type enormous = {.kind = TYPE_KIND_ARRAY,
                     .element = TYPE_INTEGER,
                     .lower = INT32_MIN,
                     .upper = INT32_MAX,
                     .line = 3,
                     .column = 8};
    assert(!wasm_type_size(&enormous, &size, &diagnostic));
    assert(strcmp(diagnostic.code, "E_LAYOUT") == 0);
    assert(diagnostic.line == 3 && diagnostic.column == 8);
    enormous.lower = 2;
    enormous.upper = 1;
    assert(!wasm_type_size(&enormous, &size, &diagnostic));
    integer_field.type = &record;
    assert(!wasm_type_size(&record, &size, &diagnostic));
    assert(strcmp(diagnostic.code, "E_LAYOUT") == 0);
    integer_field.type = TYPE_INTEGER;
    Symbol symbols[] = {{.name = "global", .type = &rows, .owner = SIZE_MAX},
                        {.name = "local", .type = TYPE_BOOLEAN, .owner = 0}};
    Program program = {.symbols = symbols, .symbol_count = 2, .routine_count = 1};
    WasmLayout layout = {0};
    assert(wasm_layout_build(&program, &layout, &diagnostic));
    assert(layout.symbols[0] == P2W_SOURCE_LIMIT && layout.symbols[1] == WASM_FRAME_HEADER);
    assert(layout.frames[0] == 32);
    assert(layout.control_base == P2W_SOURCE_LIMIT + 112);
    assert(layout.stack_base == layout.control_base + WASM_CONTROL_BYTES);
    assert(layout.stack_limit - layout.stack_base == WASM_FRAME_BUDGET);
    wasm_layout_destroy(&layout);
    symbols[1].type = &rows;
    symbols[1].by_reference = 1;
    assert(wasm_layout_build(&program, &layout, &diagnostic));
    assert(layout.frames[0] == 32); /* Reference bindings do not reserve full pointee copies. */
    assert(symbols[1].type == &rows);
    wasm_layout_destroy(&layout);
    /* A legal object can still exceed the budget after regions/headers are added. */
    enormous.lower = 0;
    enormous.upper = (WASM_MEMORY_LIMIT - WASM_CELL_SIZE) / WASM_CELL_SIZE - 1;
    assert(wasm_type_size(&enormous, &size, &diagnostic));
    symbols[0].type = &enormous;
    assert(!wasm_layout_build(&program, &layout, &diagnostic));
    assert(strcmp(diagnostic.code, "E_LAYOUT") == 0);
    wasm_layout_destroy(&layout);
    puts("Checked nested layouts, alignment, arithmetic, cycles, and region budgets passed.");
    return 0;
}
