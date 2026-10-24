/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "variant.h"
#include <stdlib.h>

/* Caches borrow logical descriptors and own only target data addresses/identities.
 * Traversal order assigns deterministic IDs independently of compiler host pointers. */
typedef struct VariantSchema {
    const VariantPart *part;
    size_t address;
    struct VariantSchema *next;
} VariantSchema;
typedef struct VariantIdentity {
    ValueType type;
    size_t identity;
    struct VariantIdentity *next;
} VariantIdentity;

/* Layout has already validated all types before emission begins. */
static size_t value_size(Wasm *w, ValueType type)
{
    size_t size = 0;
    Diagnostic diagnostic = {0};
    if (!wasm_type_size(type, &size, &diagnostic)) {
        w->data.failed = 1;
    }
    return size;
}

/* Intern a canonical identity without putting target metadata on frontend types. */
size_t variant_identity(Wasm *w, ValueType type)
{
    type = type_canonical(type);
    for (const VariantIdentity *entry = w->variant_types; entry; entry = entry->next) {
        if (entry->type == type) {
            return entry->identity;
        }
    }
    VariantIdentity *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        w->data.failed = 1;
        return 0;
    }
    entry->type = type;
    entry->identity = ++w->variant_type_count;
    entry->next = w->variant_types;
    w->variant_types = entry;
    return entry->identity;
}

/* Schemas use explicit little-endian u32 fields, never C structure padding. */
static void data_word(Wasm *w, size_t value)
{
    for (unsigned byte = 0; byte < 4; ++byte) {
        buffer_byte(&w->data, (unsigned)(value >> (byte * 8)) & 255);
    }
}

/* Emit one bounded immutable schema: arm count; then, per arm, field count,
 * nested offset/size, body size, and (offset,size,type identity) fixed-field triples. */
static size_t schema_address(Wasm *w, const VariantPart *part)
{
    for (const VariantSchema *entry = w->variant_schemas; entry; entry = entry->next) {
        if (entry->part == part) {
            return entry->address;
        }
    }
    size_t bytes = 4;
    for (const VariantArm *arm = part->arms; arm; arm = arm->next) {
        bytes += 16;
        for (const Field *field = arm->body->fields; field; field = field->next) {
            bytes += 12;
        }
    }
    size_t padding = (4 - w->data.length % 4) % 4;
    if (w->data.length > P2W_SOURCE_LIMIT || bytes + padding > P2W_SOURCE_LIMIT - w->data.length) {
        w->data_overflow = 1;
        return 0;
    }
    VariantSchema *entry = calloc(1, sizeof(*entry));
    if (!entry) {
        w->data.failed = 1;
        return 0;
    }
    while (padding--) {
        buffer_byte(&w->data, 0);
    }
    entry->part = part;
    entry->address = w->data.length;
    entry->next = w->variant_schemas;
    w->variant_schemas = entry;
    data_word(w, part->arm_count);
    for (const VariantArm *arm = part->arms; arm; arm = arm->next) {
        size_t fields = 0, nested = WASM_CELL_SIZE;
        for (const Field *field = arm->body->fields; field; field = field->next) {
            ++fields;
            nested += value_size(w, field->type);
        }
        size_t size = value_size(w, arm->body);
        data_word(w, fields);
        data_word(w, nested);
        data_word(w, size - nested);
        data_word(w, size);
        size_t offset = WASM_CELL_SIZE;
        for (const Field *field = arm->body->fields; field; field = field->next) {
            size_t child = value_size(w, field->type);
            data_word(w, offset);
            data_word(w, child);
            data_word(w, variant_identity(w, field->type));
            offset += child;
        }
    }
    return entry->address;
}

void variant_capture(Wasm *w, VariantPath *path, const Field *field, size_t base)
{
    const VariantArm *ancestors[128];
    size_t count = 0;
    for (const Type *owner = field->owner; owner && owner->arm; owner = owner->arm->part->owner) {
        if (count == 128) {
            w->code.failed = 1;
            return;
        }
        ancestors[count++] = owner->arm;
    }
    while (count) {
        const VariantArm *arm = ancestors[--count];
        if (path->count == 128) {
            w->code.failed = 1;
            return;
        }
        size_t offset = 0, address = temporary(w);
        Diagnostic diagnostic = {0};
        if (!wasm_variant_offset(arm->part, &offset, &diagnostic)) {
            w->code.failed = 1;
        }
        indexed(w, 0x20, "local.get", base);
        constant(w, 0, (int64_t)offset);
        op(w, 0x6a, "i32.add");
        indexed(w, 0x21, "local.set", address);
        path->claims[path->count].arm = arm;
        path->claims[path->count].field = arm == field->owner->arm ? field : NULL;
        path->claims[path->count++].address = address;
    }
}

/* Store a constant word in a caller-owned request/lease object. */
static void store_constant(Wasm *w, size_t object, unsigned offset, size_t value)
{
    indexed(w, 0x20, "local.get", object);
    constant(w, 0, (int32_t)value);
    memory_op(w, 0x36, "i32.store", 2, offset);
}

/* Reserve a request header (claim count, lease head, controls base/count), followed
 * by part-address/schema/one-based-arm/field-index claims. UINT32_MAX means exact arm. */
size_t variant_request(Wasm *w, const VariantPath *path, unsigned line, unsigned column)
{
    size_t object = temporary(w);
    allocate_storage(w, 16 + path->count * 16, object, line, column);
    store_constant(w, object, 0, path->count);
    indexed(w, 0x20, "local.get", object);
    indexed(w, 0x23, "global.get", 5);
    memory_op(w, 0x36, "i32.store", 2, 4);
    store_constant(w, object, 8, w->layout.control_base);
    indexed(w, 0x20, "local.get", object);
    indexed(w, 0x23, "global.get", 2);
    memory_op(w, 0x36, "i32.store", 2, 12);
    for (size_t i = 0; i < path->count; ++i) {
        const VariantArm *arm = path->claims[i].arm;
        unsigned offset = (unsigned)(16 + i * 16);
        indexed(w, 0x20, "local.get", object);
        indexed(w, 0x20, "local.get", path->claims[i].address);
        memory_op(w, 0x36, "i32.store", 2, offset);
        if (!arm) {
            store_constant(w, object, offset + 4, UINT32_MAX);
            store_constant(w, object, offset + 8, 0);
            store_constant(w, object, offset + 12, 0);
            continue;
        }
        store_constant(w, object, offset + 4, schema_address(w, arm->part));
        store_constant(w, object, offset + 8, arm->index + 1);
        size_t field_index = UINT32_MAX;
        if (path->claims[i].field) {
            field_index = 0;
            for (const Field *field = arm->body->fields; field != path->claims[i].field;
                 field = field->next) {
                ++field_index;
            }
        }
        store_constant(w, object, offset + 12, field_index);
    }
    return object;
}

/* Capture the arena top before a short-lived request; scalar stack values survive. */
static size_t request_top(Wasm *w)
{
    size_t top = temporary(w);
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x21, "local.set", top);
    return top;
}

/* Requests are consumed synchronously or retained on the same suspended JSPI stack. */
static void release_request(Wasm *w, size_t top)
{
    indexed(w, 0x20, "local.get", top);
    indexed(w, 0x24, "global.set", 1);
}

void variant_access(Wasm *w, const VariantPath *path, int pin, unsigned line, unsigned column)
{
    if (!path->count) {
        return;
    }
    size_t saved = w->next_local, top = request_top(w);
    size_t object = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", object);
    if (pin > 0) {
        indexed(w, 0x20, "local.get", top);
        constant(w, 0, (int64_t)w->layout.stack_limit);
        constant(w, 0, line);
        constant(w, 0, column);
        indexed(w, 0x10, "call", VARIANT_BIND_IMPORT);
        indexed(w, 0x21, "local.set", w->wide_local);
        indexed(w, 0x20, "local.get", w->wide_local);
        op(w, 0xa7, "i32.wrap_i64");
        indexed(w, 0x24, "global.set", 1);
        indexed(w, 0x20, "local.get", w->wide_local);
        constant(w, 1, 32);
        op(w, 0x88, "i64.shr_u");
        op(w, 0xa7, "i32.wrap_i64");
        indexed(w, 0x24, "global.set", 5);
    } else {
        constant(w, 0, pin != 0);
        constant(w, 0, line);
        constant(w, 0, column);
        indexed(w, 0x10, "call", VARIANT_ACCESS_IMPORT);
        release_request(w, top);
    }
    w->next_local = saved;
}

void variant_copy(Wasm *w, const VariantPath *path, size_t destination, size_t source, size_t size,
                  size_t capacity, unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = request_top(w);
    size_t object = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", source);
    constant(w, 0, (int64_t)size);
    constant(w, 0, (int64_t)capacity);
    indexed(w, 0x20, "local.get", object);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", VARIANT_COPY_IMPORT);
    release_request(w, top);
    w->next_local = saved;
}

void variant_read_string(Wasm *w, const VariantPath *path, size_t destination, size_t capacity,
                         int input_line, unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = request_top(w);
    size_t object = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", destination);
    constant(w, 0, (int64_t)capacity);
    constant(w, 0, input_line);
    indexed(w, 0x20, "local.get", object);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", VARIANT_READ_IMPORT);
    release_request(w, top);
    w->next_local = saved;
}

void variant_store_character(Wasm *w, const VariantPath *path, size_t destination, size_t value,
                             unsigned line, unsigned column)
{
    size_t saved = w->next_local, top = request_top(w);
    size_t object = variant_request(w, path, line, column);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", value);
    indexed(w, 0x20, "local.get", object);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", VARIANT_CHAR_STORE_IMPORT);
    release_request(w, top);
    w->next_local = saved;
}

int variant_value(ValueType type)
{
    type = type_canonical(type);
    if (type->kind == TYPE_KIND_ARRAY) {
        return variant_value(type->element);
    }
    if (type->kind == TYPE_KIND_RECORD) {
        if (type->variant) {
            return 1;
        }
        for (const Field *field = type->fields; field; field = field->next) {
            if (variant_value(field->type)) {
                return 1;
            }
        }
    }
    return 0;
}

void variant_destroy(Wasm *w)
{
    while (w->variant_schemas) {
        VariantSchema *next = w->variant_schemas->next;
        free(w->variant_schemas);
        w->variant_schemas = next;
    }
    while (w->variant_types) {
        VariantIdentity *next = w->variant_types->next;
        free(w->variant_types);
        w->variant_types = next;
    }
}
