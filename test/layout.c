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
    Type file = {.kind = TYPE_KIND_FILE, .element = TYPE_INTEGER};
    assert(wasm_type_size(&file, &size, &diagnostic) && size == 16);
    assert(wasm_type_size(TYPE_TEXT, &size, &diagnostic) && size == 16);
    file.element = NULL;
    assert(!wasm_type_size(&file, &size, &diagnostic));
    file.element = TYPE_TEXT;
    assert(!wasm_type_size(&file, &size, &diagnostic));
    file.element = TYPE_INTEGER;
    Type file_array = {.kind = TYPE_KIND_ARRAY, .element = &file, .lower = 1, .upper = 2};
    assert(!wasm_type_size(&file_array, &size, &diagnostic));
    Field file_field = {.name = "f", .type = &file};
    Type file_record = {.kind = TYPE_KIND_RECORD, .fields = &file_field};
    assert(!wasm_type_size(&file_record, &size, &diagnostic));
    Type text = {.kind = TYPE_KIND_STRING, .capacity = 17};
    assert(wasm_type_size(&text, &size, &diagnostic) && size == 48);
    text.capacity = 0;
    assert(wasm_type_size(&text, &size, &diagnostic) && size == 16);
    text.capacity = P2W_STRING_LIMIT;
    assert(wasm_type_size(&text, &size, &diagnostic) && size == P2W_STRING_LIMIT + 16);
    text.capacity++;
    assert(!wasm_type_size(&text, &size, &diagnostic));
    EnumMember last = {.name = "Last", .length = 4};
    EnumMember first = {.name = "First", .length = 5, .next = &last};
    Type enumeration = {.kind = TYPE_KIND_ENUM, .members = &first, .upper = 1};
    assert(wasm_type_size(&enumeration, &size, &diagnostic) && size == WASM_CELL_SIZE);
    enumeration.upper = 2;
    assert(!wasm_type_size(&enumeration, &size, &diagnostic));
    enumeration.upper = 1;
    Type enum_range = {.kind = TYPE_KIND_SUBRANGE, .element = &enumeration, .upper = 2};
    assert(!wasm_type_size(&enum_range, &size, &diagnostic));
    enum_range.upper = 1;
    assert(wasm_type_size(&enum_range, &size, &diagnostic) && size == WASM_CELL_SIZE);
    last.next = &first;
    assert(!wasm_type_size(&enumeration, &size, &diagnostic));
    last.next = NULL;
    Type set = {.kind = TYPE_KIND_SET, .element = TYPE_INTEGER, .lower = -128, .upper = 127};
    assert(wasm_type_size(&set, &size, &diagnostic) && size == 48);
    set.upper = 128;
    assert(!wasm_type_size(&set, &size, &diagnostic));
    set.upper = 127;
    set.element = TYPE_REAL;
    assert(!wasm_type_size(&set, &size, &diagnostic));
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
    Type union_record = {.kind = TYPE_KIND_RECORD};
    Field prefix = {.name = "fixed", .type = TYPE_INTEGER, .owner = &union_record};
    union_record.fields = &prefix;
    VariantPart part = {.owner = &union_record, .discriminator = TYPE_BOOLEAN, .arm_count = 2};
    union_record.variant = &part;
    Type first_body = {.kind = TYPE_KIND_RECORD}, second_body = {.kind = TYPE_KIND_RECORD};
    VariantArm first_arm = {.part = &part, .body = &first_body};
    VariantArm second_arm = {.part = &part, .body = &second_body, .index = 1};
    part.arms = &first_arm;
    first_arm.next = &second_arm;
    first_body.arm = &first_arm;
    second_body.arm = &second_arm;
    Field z = {.name = "z", .type = TYPE_REAL, .owner = &first_body};
    Field w = {.name = "w", .type = TYPE_REAL, .owner = &second_body};
    first_body.fields = &z;
    second_body.fields = &w;
    size_t z_offset, w_offset;
    assert(wasm_type_size(&union_record, &size, &diagnostic) && size == 64);
    assert(wasm_variant_offset(&part, &z_offset, &diagnostic) && z_offset == 32);
    assert(wasm_field_offset(&z, &z_offset, &diagnostic) && z_offset == 48);
    assert(wasm_field_offset(&w, &w_offset, &diagnostic) && w_offset == z_offset);
    part.arm_count = 1;
    assert(!wasm_type_size(&union_record, &size, &diagnostic));
    part.arm_count = 2;
    second_arm.index = 0;
    assert(!wasm_type_size(&union_record, &size, &diagnostic));
    puts("Checked nested layouts, alignment, arithmetic, cycles, and region budgets passed.");
    return 0;
}
