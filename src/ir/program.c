/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "program.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

/* Shared canonical scalar identities; future compound types belong to the arena. */
const Type p2w_type_integer = {.kind = TYPE_KIND_INTEGER};
const Type p2w_type_boolean = {.kind = TYPE_KIND_BOOLEAN};
const Type p2w_type_real = {.kind = TYPE_KIND_REAL};
const Type p2w_type_character = {.kind = TYPE_KIND_CHARACTER};
const Type p2w_type_nil = {.kind = TYPE_KIND_NIL};
const Type p2w_type_text = {.kind = TYPE_KIND_TEXT};

ValueType type_canonical(ValueType type)
{
    for (unsigned depth = 0; type && depth < 128; ++depth) {
        if (type->kind != TYPE_KIND_ALIAS) {
            return type;
        }
        type = type->element;
    }
    return NULL;
}

ValueType type_value(ValueType type)
{
    type = type_canonical(type);
    return type && type->kind == TYPE_KIND_SUBRANGE ? type_canonical(type->element) : type;
}

int type_ordinal(ValueType type)
{
    type = type_value(type);
    return type && (type == TYPE_INTEGER || type == TYPE_BOOLEAN || type == TYPE_CHARACTER ||
                    type->kind == TYPE_KIND_ENUM);
}

void *program_allocate(Program *program, size_t size, Diagnostic *diagnostic)
{
    const char *code = "E_LIMIT", *message = "Program is too complex.";
    if (++program->node_count <= 65536 && size <= SIZE_MAX - sizeof(Allocation)) {
        Allocation *allocation = calloc(1, sizeof(*allocation) + size);
        if (allocation) {
            allocation->next = program->allocations;
            program->allocations = allocation;
            return allocation->data;
        }
        code = "E_MEMORY";
        message = "Compiler allocation failed.";
    }
    snprintf(diagnostic->code, sizeof(diagnostic->code), "%s", code);
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    return NULL;
}

char *program_text(Program *program, size_t size, Diagnostic *diagnostic)
{
    if (size > P2W_CONSTANT_TEXT_LIMIT - program->text_bytes) {
        snprintf(diagnostic->code, sizeof(diagnostic->code), "E_LIMIT");
        snprintf(diagnostic->message, sizeof(diagnostic->message),
                 "Constant text exceeds the compiler byte budget.");
        return NULL;
    }
    program->text_bytes += size;
    return program_allocate(program, size, diagnostic);
}

size_t text_length(const char *text, size_t length)
{
    size_t count = 0;
    for (size_t i = 0; i < length; ++i) {
        count += ((unsigned char)text[i] & 0xc0) != 0x80;
    }
    return count;
}

int text_compare(const char *a, size_t a_length, const char *b, size_t b_length)
{
    /* UTF-8 preserves scalar ordering in unsigned byte lexicographic order. */
    size_t shared = a_length < b_length ? a_length : b_length;
    int order = memcmp(a, b, shared);
    return order ? order : (a_length > b_length) - (a_length < b_length);
}

int character_valid(int32_t scalar)
{
    return scalar >= 0 && scalar <= 0x10ffff && (scalar < 0xd800 || scalar > 0xdfff);
}

size_t text_decode(const char *text, int32_t *scalar)
{
    unsigned first = (unsigned char)text[0];
    size_t width = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
    unsigned value = first & (width == 1 ? 0x7f : width == 2 ? 0x1f : width == 3 ? 0x0f : 0x07);
    for (size_t i = 1; i < width; ++i) {
        value = (value << 6) | ((unsigned char)text[i] & 0x3f);
    }
    *scalar = (int32_t)value;
    return width;
}

size_t text_encode(int32_t scalar, char *text)
{
    unsigned value = (unsigned)scalar;
    size_t width = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
    if (width == 1) {
        text[0] = (char)value;
    } else {
        for (size_t i = width - 1; i > 0; --i) {
            text[i] = (char)(0x80 | (value & 0x3f));
            value >>= 6;
        }
        text[0] = (char)((width == 2 ? 0xc0 : width == 3 ? 0xe0 : 0xf0) | value);
    }
    return width;
}

void program_destroy(Program *program)
{
    Allocation *allocation = program->allocations;
    while (allocation) {
        Allocation *next = allocation->next;
        free(allocation);
        allocation = next;
    }
    free(program->symbols);
    memset(program, 0, sizeof(*program));
}
