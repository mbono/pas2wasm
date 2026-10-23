/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "emitter.h"
#include "ir/number.h"
#include <stdio.h>
#include <string.h>

/* Absolute Wasm label depths for the nearest Pascal loop. Entries borrow their
 * parent and live on the recursive lowering stack. */
typedef struct Loop {
    unsigned stop, resume;
    size_t control; /* Counted control to invalidate on routine exit; SIZE_MAX otherwise. */
    const struct Loop *outer;
} Loop;

static const Import imports[] = {{"write_utf8", "ii", 0},
                                 {"write_i32", "i", 0},
                                 {"write_bool", "i", 0},
                                 {"newline", "", 0},
                                 {"read_i32", "ii", 0x7f},
                                 {"discard_line", "ii", 0},
                                 {"fail", "iii", 0},
                                 {"write_f64", "d", 0},
                                 {"read_f64", "ii", 0x7c},
                                 {"string_assign", "iiiii", 0},
                                 {"string_concat", "iiiiii", 0},
                                 {"string_compare", "iiii", 0x7f},
                                 {"string_length", "iii", 0x7f},
                                 {"string_read", "iiii", 0},
                                 {"string_write", "iii", 0},
                                 {"string_index", "iiii", 0x7f},
                                 {"char_load", "iii", 0x7f},
                                 {"char_store", "iiii", 0},
                                 {"write_char", "iii", 0},
                                 {"read_char", "ii", 0x7f},
                                 {"string_char", "iiiii", 0},
                                 {"string_slice_size", "iiiii", 0x7f},
                                 {"string_slice", "iiiiiii", 0},
                                 {"stream_i32", "iii", 0x7f},
                                 {"stream_f64", "iii", 0x7c},
                                 {"stream_char", "iii", 0x7f},
                                 {"stream_string", "iiiii", 0}};
enum {
    IMPORT_COUNT = sizeof(imports) / sizeof(*imports),
    RUN_TYPE = IMPORT_COUNT,
    ROUTINE_TYPE_BASE = IMPORT_COUNT + 1
};

/* All heap ownership helpers are module-private functions after Pascal routines. */
size_t heap_index(const Wasm *w, unsigned helper)
{
    return IMPORT_COUNT + 1 + w->program->routine_count + helper;
}

/* Encode an unsigned LEB128 integer. */
void uleb(Buffer *buffer, uint64_t value)
{
    do {
        unsigned byte = (unsigned)(value & 127);
        value >>= 7;
        buffer_byte(buffer, byte | (value ? 128 : 0));
    } while (value);
}

/* Encode signed LEB128 without relying on signed right-shift behavior. */
static void sleb(Buffer *buffer, int64_t value)
{
    int more;
    do {
        unsigned byte = (unsigned)((uint64_t)value & 127);
        value = value >= 0 ? value / 128 : -1 - ((-1 - value) / 128);
        more = !((value == 0 && !(byte & 64)) || (value == -1 && (byte & 64)));
        buffer_byte(buffer, byte | (more ? 128 : 0));
    } while (more);
}

/* Encode a length-prefixed UTF-8 module name. */
static void name(Buffer *buffer, const char *text)
{
    size_t length = strlen(text);
    uleb(buffer, length);
    buffer_append(buffer, text, length);
}

/* Transfer a completed payload into a binary section and release the payload. */
static void section(Buffer *module, unsigned id, Buffer *payload)
{
    if (payload->failed) {
        module->failed = 1;
    }
    buffer_byte(module, id);
    uleb(module, payload->length);
    buffer_append(module, payload->data, payload->length);
    buffer_free(payload);
}

/* Start an indented WAT line only when text output was requested. */
static void indent(Wasm *w)
{
    if (w->emit_wat) {
        for (unsigned i = 0; i < w->indent; ++i) {
            buffer_append(&w->text, "  ", 2);
        }
    }
}

/* Emit a no-immediate instruction to binary and the optional text view. */
void op(Wasm *w, unsigned opcode, const char *text)
{
    buffer_byte(&w->code, opcode);
    if (w->emit_wat) {
        indent(w);
        buffer_printf(&w->text, "%s\n", text);
    }
}

/* Emit an instruction with an unsigned index/depth immediate. */
void indexed(Wasm *w, unsigned opcode, const char *text, size_t index)
{
    buffer_byte(&w->code, opcode);
    uleb(&w->code, index);
    if (w->emit_wat) {
        indent(w);
        buffer_printf(&w->text, "%s %zu\n", text, index);
    }
}

/* Emit a signed integer constant of the requested Wasm width. */
void constant(Wasm *w, int wide, int64_t value)
{
    buffer_byte(&w->code, wide ? 0x42 : 0x41);
    sleb(&w->code, value);
    if (w->emit_wat) {
        indent(w);
        buffer_printf(&w->text, "i%d.const %lld\n", wide ? 64 : 32, (long long)value);
    }
}

/* Encode exact binary64 bits; hexadecimal WAT avoids decimal host formatting. */
static void real_constant(Wasm *w, uint64_t bits)
{
    buffer_byte(&w->code, 0x44);
    for (unsigned i = 0; i < 8; ++i) {
        buffer_byte(&w->code, (unsigned)(bits >> (i * 8)) & 255);
    }
    if (w->emit_wat) {
        unsigned exponent = (unsigned)((bits >> 52) & 2047);
        uint64_t fraction = bits & ((UINT64_C(1) << 52) - 1);
        indent(w);
        buffer_printf(&w->text, "f64.const %s0x%d.%013llxp%+d\n", bits >> 63 ? "-" : "",
                      exponent ? 1 : 0, (unsigned long long)fraction,
                      exponent ? (int)exponent - 1023 : -1022);
    }
}

/* Open a void structured control instruction. */
void open_control(Wasm *w, unsigned opcode, const char *text)
{
    op(w, opcode, text);
    buffer_byte(&w->code, 0x40);
    ++w->indent;
    ++w->depth;
}

/* Close the innermost structured control block. */
void close_control(Wasm *w)
{
    --w->indent;
    --w->depth;
    op(w, 0x0b, "end");
}

/* Emit a stable runtime failure followed by unreachable if a bad host returns. */
static void failure(Wasm *w, unsigned code, unsigned line, unsigned column)
{
    constant(w, 0, code);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", 6);
    op(w, 0x00, "unreachable");
}

/* Fail when the Boolean currently on the stack is true. */
static void check(Wasm *w, unsigned code, unsigned line, unsigned column)
{
    open_control(w, 0x04, "if");
    failure(w, code, line, column);
    close_control(w);
}

/* Narrow the stack's i64 value only after checking the Pascal integer range. */
static void narrow(Wasm *w, const Expr *e)
{
    indexed(w, 0x21, "local.set", w->wide_local);
    indexed(w, 0x20, "local.get", w->wide_local);
    constant(w, 1, INT32_MIN);
    op(w, 0x53, "i64.lt_s");
    indexed(w, 0x20, "local.get", w->wide_local);
    constant(w, 1, INT32_MAX);
    op(w, 0x55, "i64.gt_s");
    op(w, 0x72, "i32.or");
    check(w, 2, e->line, e->column);
    indexed(w, 0x20, "local.get", w->wide_local);
    op(w, 0xa7, "i32.wrap_i64");
}

/* Emit a core Wasm memory access; both encodings share alignment/offset data. */
void memory_op(Wasm *w, unsigned opcode, const char *text, unsigned align, unsigned offset)
{
    buffer_byte(&w->code, opcode);
    uleb(&w->code, align);
    uleb(&w->code, offset);
    if (w->emit_wat) {
        indent(w);
        buffer_printf(&w->text, "%s offset=%u\n", text, offset);
    }
}

/* Follow static links to the correct live lexical activation, never a singleton
 * frame. Frontend visibility guarantees that owner is on this parent chain. */
static void lexical_frame(Wasm *w, size_t owner)
{
    indexed(w, 0x20, "local.get", w->frame_local);
    const Routine *scope = w->routine;
    while (scope && scope->index != owner) {
        memory_op(w, 0x28, "i32.load", 2, 0);
        scope = scope->parent;
    }
    if (!scope) {
        w->code.failed = 1;
    }
}

/* Push a resolved symbol address; globals have absolute offsets, locals are
 * relative to their owning lexical activation. Later selectors build on the same storage path. */
static void binding_address(Wasm *w, size_t symbol)
{
    const Symbol *value = &w->program->symbols[symbol];
    constant(w, 0, (int64_t)w->layout.symbols[symbol]);
    if (value->owner != SIZE_MAX) {
        lexical_frame(w, value->owner);
        op(w, 0x6a, "i32.add");
    }
}

/* Reference bindings point at the actual storage and its owning written root. */
static void symbol_address(Wasm *w, size_t symbol)
{
    binding_address(w, symbol);
    if (w->program->symbols[symbol].by_reference) {
        memory_op(w, 0x28, "i32.load", 2, 0);
    }
}

/* Propagate writes through forwarded references to the original result/root. */
static void root_address(Wasm *w, size_t symbol)
{
    binding_address(w, symbol);
    if (w->program->symbols[symbol].by_reference) {
        memory_op(w, 0x28, "i32.load", 2, 4);
    }
}

/* Mark the owning object written without changing any sibling leaf's flags. */
static void mark_root(Wasm *w, size_t symbol)
{
    root_address(w, symbol);
    constant(w, 0, 1);
    memory_op(w, 0x3a, "i32.store8", 0, 8);
}

/* Access a scalar payload or initialization byte. A store consumes exactly one
 * value; saving it locally preserves any earlier live operands on the Wasm stack. */
static void variable(Wasm *w, size_t symbol, int flag, int store)
{
    int real = !flag && w->program->symbols[symbol].type == TYPE_REAL;
    int pointer = !flag && w->program->symbols[symbol].type->kind == TYPE_KIND_POINTER;
    size_t local = real ? w->real_local : pointer ? w->wide_local : w->left_local;
    if (store) {
        indexed(w, 0x21, "local.set", local);
    }
    symbol_address(w, symbol);
    if (store) {
        indexed(w, 0x20, "local.get", local);
        memory_op(w,
                  flag      ? 0x3a
                  : pointer ? 0x37
                  : real    ? 0x39
                            : 0x36,
                  flag      ? "i32.store8"
                  : pointer ? "i64.store"
                  : real    ? "f64.store"
                            : "i32.store",
                  flag              ? 0
                  : real || pointer ? 3
                                    : 2,
                  flag ? 8 : 0);
    } else {
        memory_op(w,
                  flag      ? 0x2d
                  : pointer ? 0x29
                  : real    ? 0x2b
                            : 0x28,
                  flag      ? "i32.load8_u"
                  : pointer ? "i64.load"
                  : real    ? "f64.load"
                            : "i32.load",
                  flag              ? 0
                  : real || pointer ? 3
                                    : 2,
                  flag ? 8 : 0);
    }
}

/* Borrow a scratch local until the caller restores next_local. Recursive lowering
 * cannot overwrite a live destination/address because it allocates above this. */
static size_t temporary(Wasm *w)
{
    size_t local = w->next_local++;
    if (w->next_local > w->local_count) {
        w->local_count = w->next_local;
    }
    return local;
}

/* Every stored type was validated when constructing the backend storage map. */
static size_t type_size(Wasm *w, ValueType type)
{
    size_t size = 0;
    Diagnostic diagnostic = {0};
    if (!wasm_type_size(type, &size, &diagnostic)) {
        w->code.failed = 1;
    }
    return size;
}

/* Aggregate values use private copy addresses; scalar values stay on the stack. */
static int aggregate(ValueType type)
{
    return type && (type->kind == TYPE_KIND_ARRAY || type->kind == TYPE_KIND_RECORD ||
                    type->kind == TYPE_KIND_STRING);
}

/* Index expressions and loads can contain each other. */
static void emit_expression(Wasm *w, const Expr *e);

/* Character paths use caller-owned scalar-index descriptors rather than byte offsets. */
static void character_view(Wasm *w, const Lvalue *value, size_t root);
/* Optional root local follows the last dereference, not the originating pointer cell. */
static void emit_path(Wasm *w, const Lvalue *value, size_t root);

/* Evaluate a storage path once, checking each index before offset arithmetic.
 * Validated object sizes guarantee in-range products/sums fit program memory. */
static void emit_lvalue(Wasm *w, const Lvalue *value)
{
    emit_path(w, value, SIZE_MAX);
}

/* Resolve a path while retaining the correct owning written root across selectors. */
static void emit_path(Wasm *w, const Lvalue *value, size_t root)
{
    if (value->kind == LV_SYMBOL) {
        symbol_address(w, value->symbol);
        if (root != SIZE_MAX) {
            root_address(w, value->symbol);
            indexed(w, 0x21, "local.set", root);
        }
        return;
    }
    if (value->kind == LV_TEMPORARY) {
        emit_expression(w, value->value);
        return;
    }
    if (value->kind == LV_CHARACTER) {
        character_view(w, value, root);
        return;
    }
    if (value->kind == LV_DEREFERENCE) {
        size_t saved = w->next_local;
        if (value->base->kind == LV_TEMPORARY) {
            emit_expression(w, value->base->value);
        } else {
            size_t address = temporary(w);
            emit_path(w, value->base, root);
            indexed(w, 0x22, "local.tee", address);
            memory_op(w, 0x2d, "i32.load8_u", 0, 8);
            op(w, 0x45, "i32.eqz");
            check(w, 1, value->line, value->column);
            indexed(w, 0x20, "local.get", address);
            memory_op(w, 0x29, "i64.load", 3, 0);
        }
        constant(w, 0, (int64_t)type_size(w, value->type));
        constant(w, 0, value->line);
        constant(w, 0, value->column);
        indexed(w, 0x10, "call", heap_index(w, HEAP_BORROW));
        if (root != SIZE_MAX) {
            indexed(w, 0x22, "local.tee", root);
        }
        w->next_local = saved;
        return;
    }
    emit_path(w, value->base, root);
    if (value->kind == LV_FIELD) {
        size_t offset = WASM_CELL_SIZE;
        for (const Field *field = value->base->type->fields; field != value->field;
             field = field->next) {
            offset += type_size(w, field->type);
        }
        constant(w, 0, (int64_t)offset);
        op(w, 0x6a, "i32.add");
        return;
    }
    emit_expression(w, value->index);
    indexed(w, 0x21, "local.set", w->left_local);
    indexed(w, 0x20, "local.get", w->left_local);
    constant(w, 0, value->base->type->lower);
    op(w, 0x48, "i32.lt_s");
    indexed(w, 0x20, "local.get", w->left_local);
    constant(w, 0, value->base->type->upper);
    op(w, 0x4a, "i32.gt_s");
    op(w, 0x72, "i32.or");
    check(w, 8, value->line, value->column);
    indexed(w, 0x20, "local.get", w->left_local);
    constant(w, 0, value->base->type->lower);
    op(w, 0x6b, "i32.sub");
    constant(w, 0, (int64_t)type_size(w, value->type));
    op(w, 0x6c, "i32.mul");
    constant(w, 0, WASM_CELL_SIZE);
    op(w, 0x6a, "i32.add");
    op(w, 0x6a, "i32.add");
}

/* Logical reference parameters occupy target/root slots; value arguments use one. */
static size_t parameter_slots(const Routine *routine)
{
    size_t slots =
        (routine->parent != NULL) + (routine->is_function && aggregate(routine->return_type));
    for (size_t i = 0; i < routine->parameter_count; ++i) {
        slots += routine->parameter_modes[i] ? 2 : 1;
    }
    return slots;
}

/* Scalar Wasm types are a backend choice; aggregates travel as private addresses. */
static unsigned scalar_type(ValueType type)
{
    return type == TYPE_REAL ? 0x7c : type->kind == TYPE_KIND_POINTER ? 0x7e : 0x7f;
}

/* Keep binary and WAT signatures identical, including links, results, and refs. */
static void signature_type(Buffer *output, unsigned type, int wat)
{
    if (wat) {
        buffer_printf(output, type == 0x7c ? " f64" : type == 0x7e ? " i64" : " i32");
    } else {
        buffer_byte(output, type);
    }
}

/* Emit physical parameter types in their source-independent calling order. */
static void signature_parameters(Buffer *output, const Routine *routine, int wat)
{
    size_t prefix =
        (routine->parent != NULL) + (routine->is_function && aggregate(routine->return_type));
    for (size_t i = 0; i < prefix; ++i) {
        signature_type(output, 0x7f, wat);
    }
    for (size_t i = 0; i < routine->parameter_count; ++i) {
        if (routine->parameter_modes[i]) {
            signature_type(output, 0x7f, wat);
            signature_type(output, 0x7f, wat);
        } else {
            signature_type(output, scalar_type(routine->parameter_types[i]), wat);
        }
    }
}

/* Calls are shared by statement and expression lowering. */
static void call_routine(Wasm *w, const Routine *routine, unsigned line, unsigned column);
/* Snapshot arguments and retain an aggregate result until its consumer finishes. */
static void emit_call(Wasm *w, const Routine *routine, const Expr *arguments, unsigned line,
                      unsigned column);
/* Allocate cleared transient/frame storage without changing the current frame base. */
static void allocate_storage(Wasm *w, size_t size, size_t destination, unsigned line,
                             unsigned column);

/* Allocate a runtime byte count already on the stack, preserving caller locals. */
static void allocate_dynamic(Wasm *w, size_t destination, unsigned line, unsigned column);
/* Snapshot a string stack address into a fresh actual-length value; return its local. */
static size_t snapshot_string(Wasm *w, unsigned line, unsigned column);
/* Lower string operations separately from numeric Wasm stack values. */
static void string_expression(Wasm *w, const Expr *e);

/* Retain finite binary64 results and diagnose overflow/NaN before consumption. */
static void real_finite(Wasm *w, const Expr *e)
{
    indexed(w, 0x21, "local.set", w->real_local);
    indexed(w, 0x20, "local.get", w->real_local);
    indexed(w, 0x20, "local.get", w->real_local);
    op(w, 0x62, "f64.ne");
    indexed(w, 0x20, "local.get", w->real_local);
    op(w, 0x99, "f64.abs");
    real_constant(w, UINT64_C(0x7fefffffffffffff));
    op(w, 0x64, "f64.gt");
    op(w, 0x72, "i32.or");
    check(w, 10, e->line, e->column);
    indexed(w, 0x20, "local.get", w->real_local);
}

/* Round first, then validate before using Wasm's potentially trapping conversion. */
static void real_to_integer(Wasm *w, const Expr *e)
{
    emit_expression(w, e->left);
    op(w, e->kind == EX_ROUND ? 0x9e : 0x9d, e->kind == EX_ROUND ? "f64.nearest" : "f64.trunc");
    indexed(w, 0x21, "local.set", w->real_local);
    indexed(w, 0x20, "local.get", w->real_local);
    real_constant(w, number_bits((double)INT32_MIN));
    op(w, 0x63, "f64.lt");
    indexed(w, 0x20, "local.get", w->real_local);
    real_constant(w, number_bits((double)INT32_MAX));
    op(w, 0x64, "f64.gt");
    op(w, 0x72, "i32.or");
    indexed(w, 0x20, "local.get", w->real_local);
    indexed(w, 0x20, "local.get", w->real_local);
    op(w, 0x62, "f64.ne");
    op(w, 0x72, "i32.or");
    check(w, 11, e->line, e->column);
    indexed(w, 0x20, "local.get", w->real_local);
    op(w, 0xaa, "i32.trunc_f64_s");
}

/* Lower typed real arithmetic/comparisons with exact operand order and zero checks. */
static void real_expression(Wasm *w, const Expr *e)
{
    emit_expression(w, e->left);
    if (e->kind == EX_NEGATE) {
        op(w, 0x9a, "f64.neg");
        return;
    }
    emit_expression(w, e->right);
    if (e->kind == EX_REAL_DIVIDE) {
        indexed(w, 0x22, "local.tee", w->real_local);
        real_constant(w, 0);
        op(w, 0x61, "f64.eq");
        check(w, 3, e->line, e->column);
        indexed(w, 0x20, "local.get", w->real_local);
    }
    switch (e->kind) {
    case EX_EQUAL:
        op(w, 0x61, "f64.eq");
        return;
    case EX_UNEQUAL:
        op(w, 0x62, "f64.ne");
        return;
    case EX_LESS:
        op(w, 0x63, "f64.lt");
        return;
    case EX_GREATER:
        op(w, 0x64, "f64.gt");
        return;
    case EX_LEQUAL:
        op(w, 0x65, "f64.le");
        return;
    case EX_GEQUAL:
        op(w, 0x66, "f64.ge");
        return;
    case EX_ADD:
        op(w, 0xa0, "f64.add");
        break;
    case EX_SUBTRACT:
        op(w, 0xa1, "f64.sub");
        break;
    case EX_MULTIPLY:
        op(w, 0xa2, "f64.mul");
        break;
    case EX_REAL_DIVIDE:
        op(w, 0xa3, "f64.div");
        break;
    default:
        w->code.failed = 1;
        return;
    }
    real_finite(w, e);
}

/* Lower an expression left-to-right; checks use scratch slots only after children
 * finish, so nested arithmetic cannot overwrite a live intermediate value. */
static void emit_expression(Wasm *w, const Expr *e)
{
    if (e->kind == EX_TEXT || e->kind == EX_STRING_CONVERT || e->kind == EX_LENGTH ||
        e->kind == EX_CHARACTER_STRING || e->kind == EX_SLICE ||
        (e->kind != EX_CALL && e->left && e->left->type->kind == TYPE_KIND_STRING)) {
        string_expression(w, e);
        return;
    }
    if (e->kind == EX_NIL) {
        constant(w, 1, 0);
        return;
    }
    if (e->kind == EX_INTEGER) {
        constant(w, 0, e->integer);
        return;
    }
    if (e->kind == EX_REAL) {
        real_constant(w, e->real_bits);
        return;
    }
    if (e->kind == EX_ORDINAL) {
        emit_expression(w, e->left);
        return;
    }
    if (e->kind == EX_CHARACTER) {
        emit_expression(w, e->left);
        indexed(w, 0x22, "local.tee", w->left_local);
        constant(w, 0, 0x10ffff);
        op(w, 0x4b, "i32.gt_u");
        indexed(w, 0x20, "local.get", w->left_local);
        constant(w, 0, 0xd800);
        op(w, 0x4f, "i32.ge_u");
        indexed(w, 0x20, "local.get", w->left_local);
        constant(w, 0, 0xdfff);
        op(w, 0x4d, "i32.le_u");
        op(w, 0x71, "i32.and");
        op(w, 0x72, "i32.or");
        check(w, 13, e->line, e->column);
        indexed(w, 0x20, "local.get", w->left_local);
        return;
    }
    if (e->kind == EX_TO_REAL) {
        emit_expression(w, e->left);
        op(w, 0xb7, "f64.convert_i32_s");
        return;
    }
    if (e->kind == EX_TRUNCATE || e->kind == EX_ROUND) {
        real_to_integer(w, e);
        return;
    }
    if (e->kind == EX_LOAD) {
        size_t saved = w->next_local;
        size_t address = temporary(w);
        emit_lvalue(w, e->lvalue);
        if (e->type == TYPE_CHARACTER) {
            constant(w, 0, e->line);
            constant(w, 0, e->column);
            indexed(w, 0x10, "call", 16);
            w->next_local = saved;
            return;
        }
        if (aggregate(e->type) && e->type->kind != TYPE_KIND_STRING) {
            w->next_local = saved;
            return; /* Aggregate values are copy sources; no leaf is read here. */
        }
        indexed(w, 0x22, "local.tee", address);
        memory_op(w, 0x2d, "i32.load8_u", 0, 8);
        op(w, 0x45, "i32.eqz");
        check(w, 1, e->line, e->column);
        indexed(w, 0x20, "local.get", address);
        if (e->type->kind == TYPE_KIND_STRING) {
            w->next_local = saved;
            return;
        }
        unsigned type = scalar_type(e->type);
        memory_op(w,
                  type == 0x7c   ? 0x2b
                  : type == 0x7e ? 0x29
                                 : 0x28,
                  type == 0x7c   ? "f64.load"
                  : type == 0x7e ? "i64.load"
                                 : "i32.load",
                  type == 0x7f ? 2 : 3, 0);
        w->next_local = saved;
        return;
    }
    if (e->kind == EX_CALL) {
        emit_call(w, e->routine, e->left, e->line, e->column);
        return;
    }
    if (e->left && (e->left->type->kind == TYPE_KIND_POINTER || e->left->type == TYPE_NIL)) {
        emit_expression(w, e->left);
        emit_expression(w, e->right);
        op(w, e->kind == EX_EQUAL ? 0x51 : 0x52, e->kind == EX_EQUAL ? "i64.eq" : "i64.ne");
        return;
    }
    if (e->left && e->left->type == TYPE_REAL) {
        real_expression(w, e);
        return;
    }
    if (e->kind == EX_NOT) {
        emit_expression(w, e->left);
        op(w, 0x45, "i32.eqz");
        return;
    }
    if (e->kind == EX_NEGATE) {
        constant(w, 1, 0);
        emit_expression(w, e->left);
        op(w, 0xac, "i64.extend_i32_s");
        op(w, 0x7d, "i64.sub");
        narrow(w, e);
        return;
    }
    int wide = e->kind == EX_ADD || e->kind == EX_SUBTRACT || e->kind == EX_MULTIPLY;
    emit_expression(w, e->left);
    if (wide) {
        op(w, 0xac, "i64.extend_i32_s");
    }
    emit_expression(w, e->right);
    if (wide) {
        op(w, 0xac, "i64.extend_i32_s");
        if (e->kind == EX_ADD) {
            op(w, 0x7c, "i64.add");
        } else if (e->kind == EX_SUBTRACT) {
            op(w, 0x7d, "i64.sub");
        } else {
            op(w, 0x7e, "i64.mul");
        }
        narrow(w, e);
        return;
    }
    if (e->kind == EX_DIVIDE || e->kind == EX_MODULO) {
        indexed(w, 0x21, "local.set", w->right_local);
        indexed(w, 0x21, "local.set", w->left_local);
        indexed(w, 0x20, "local.get", w->right_local);
        op(w, 0x45, "i32.eqz");
        check(w, 3, e->line, e->column);
        indexed(w, 0x20, "local.get", w->left_local);
        constant(w, 0, 0);
        op(w, 0x48, "i32.lt_s");
        indexed(w, 0x20, "local.get", w->right_local);
        constant(w, 0, 0);
        op(w, 0x48, "i32.lt_s");
        op(w, 0x72, "i32.or");
        check(w, 4, e->line, e->column);
        indexed(w, 0x20, "local.get", w->left_local);
        indexed(w, 0x20, "local.get", w->right_local);
        op(w, e->kind == EX_DIVIDE ? 0x6d : 0x6f, e->kind == EX_DIVIDE ? "i32.div_s" : "i32.rem_s");
        return;
    }
    switch (e->kind) {
    case EX_AND:
        op(w, 0x71, "i32.and");
        break;
    case EX_OR:
        op(w, 0x72, "i32.or");
        break;
    case EX_EQUAL:
        op(w, 0x46, "i32.eq");
        break;
    case EX_UNEQUAL:
        op(w, 0x47, "i32.ne");
        break;
    case EX_LESS:
        op(w, 0x48, "i32.lt_s");
        break;
    case EX_LEQUAL:
        op(w, 0x4c, "i32.le_s");
        break;
    case EX_GREATER:
        op(w, 0x4a, "i32.gt_s");
        break;
    case EX_GEQUAL:
        op(w, 0x4e, "i32.ge_s");
        break;
    default:
        break; /* Frontend validation excludes unsupported operators here. */
    }
}

/* Mark a variable initialized after consuming its new value from the stack. */
static void assign(Wasm *w, size_t symbol)
{
    variable(w, symbol, 0, 1);
    constant(w, 0, 1);
    variable(w, symbol, 1, 1);
    if (w->program->symbols[symbol].by_reference) {
        mark_root(w, symbol);
    }
}

/* Copy whole value bytes with memmove ordering. Protection lives in the external
 * address table; lifetime metadata must remain outside copied value objects. */
static void copy_value(Wasm *w, size_t destination, size_t size)
{
    size_t source = temporary(w), cursor = temporary(w), step = temporary(w);
    indexed(w, 0x21, "local.set", source);
    constant(w, 0, 0);
    indexed(w, 0x21, "local.set", cursor);
    constant(w, 0, 1);
    indexed(w, 0x21, "local.set", step);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", source);
    op(w, 0x4b, "i32.gt_u");
    open_control(w, 0x04, "if");
    constant(w, 0, (int64_t)size - 1);
    indexed(w, 0x21, "local.set", cursor);
    constant(w, 0, -1);
    indexed(w, 0x21, "local.set", step);
    close_control(w);
    open_control(w, 0x03, "loop");
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", cursor);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x20, "local.get", source);
    indexed(w, 0x20, "local.get", cursor);
    op(w, 0x6a, "i32.add");
    memory_op(w, 0x2d, "i32.load8_u", 0, 0);
    memory_op(w, 0x3a, "i32.store8", 0, 0);
    indexed(w, 0x20, "local.get", cursor);
    indexed(w, 0x20, "local.get", step);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x22, "local.tee", cursor);
    constant(w, 0, (int64_t)size);
    op(w, 0x49, "i32.lt_u");
    indexed(w, 0x0d, "br_if", 0);
    close_control(w);
}

/* Copy strings by actual length into the destination's declared capacity. */
static void copy_typed(Wasm *w, size_t destination, ValueType type, unsigned line, unsigned column)
{
    if (type->kind != TYPE_KIND_STRING) {
        copy_value(w, destination, type_size(w, type));
        return;
    }
    size_t source = temporary(w);
    indexed(w, 0x21, "local.set", source);
    indexed(w, 0x20, "local.get", destination);
    constant(w, 0, (int64_t)type->capacity);
    indexed(w, 0x20, "local.get", source);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", 9);
}

/* Check object intervals against active controls across all calls/aliases.
 * Metadata lives outside payloads, so future UTF-8 bytes cannot mimic a guard. */
static void check_writable(Wasm *w, size_t destination, size_t size, unsigned line, unsigned column)
{
    size_t saved = w->next_local;
    size_t cursor = temporary(w), control = temporary(w);
    constant(w, 0, 0);
    indexed(w, 0x21, "local.set", cursor);
    open_control(w, 0x02, "block");
    open_control(w, 0x03, "loop");
    indexed(w, 0x20, "local.get", cursor);
    indexed(w, 0x23, "global.get", 2);
    op(w, 0x4f, "i32.ge_u");
    indexed(w, 0x0d, "br_if", 1);
    constant(w, 0, (int64_t)w->layout.control_base);
    indexed(w, 0x20, "local.get", cursor);
    constant(w, 0, 4);
    op(w, 0x6c, "i32.mul");
    op(w, 0x6a, "i32.add");
    memory_op(w, 0x28, "i32.load", 2, 0);
    indexed(w, 0x22, "local.tee", control);
    indexed(w, 0x20, "local.get", destination);
    op(w, 0x4f, "i32.ge_u");
    indexed(w, 0x20, "local.get", control);
    indexed(w, 0x20, "local.get", destination);
    constant(w, 0, (int64_t)size);
    op(w, 0x6a, "i32.add");
    op(w, 0x49, "i32.lt_u");
    op(w, 0x71, "i32.and");
    check(w, 9, line, column);
    indexed(w, 0x20, "local.get", cursor);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x21, "local.set", cursor);
    indexed(w, 0x0c, "br", 0);
    close_control(w);
    close_control(w);
    w->next_local = saved;
}

/* Character views contain no integer cells, so cannot overlap counted controls.
 * Actual char cells and all other values retain the ordinary interval check. */
static void check_target(Wasm *w, size_t destination, ValueType type, unsigned line,
                         unsigned column)
{
    if (type == TYPE_CHARACTER) {
        indexed(w, 0x20, "local.get", destination);
        constant(w, 0, 0);
        op(w, 0x4e, "i32.ge_s");
        open_control(w, 0x04, "if");
    }
    check_writable(w, destination, type_size(w, type), line, column);
    if (type == TYPE_CHARACTER) {
        close_control(w);
    }
}

/* Resolve the destination before evaluating the source/input and retain its
 * address across arbitrary calls. Indexed writes also mark their root touched. */
static void emit_assignment(Wasm *w, const Lvalue *target, const Expr *source, unsigned line,
                            unsigned column, int input_line)
{
    size_t saved = w->next_local;
    size_t destination = temporary(w), root = temporary(w);
    emit_path(w, target, root);
    indexed(w, 0x21, "local.set", destination);
    check_target(w, destination, target->type, target->line ? target->line : line,
                 target->column ? target->column : column);
    if (target->type == TYPE_CHARACTER) {
        size_t value = temporary(w);
        if (source) {
            emit_expression(w, source);
        } else {
            constant(w, 0, input_line);
            constant(w, 0, line);
            constant(w, 0, column);
            indexed(w, 0x10, "call", 25);
        }
        indexed(w, 0x21, "local.set", value);
        indexed(w, 0x20, "local.get", destination);
        indexed(w, 0x20, "local.get", value);
        constant(w, 0, line);
        constant(w, 0, column);
        indexed(w, 0x10, "call", 17);
    } else if (target->type->kind == TYPE_KIND_STRING) {
        if (source) {
            emit_expression(w, source);
            copy_typed(w, destination, target->type, line, column);
        } else {
            indexed(w, 0x20, "local.get", destination);
            constant(w, 0, (int64_t)target->type->capacity);
            constant(w, 0, input_line);
            constant(w, 0, line);
            constant(w, 0, column);
            indexed(w, 0x10, "call", 26);
        }
    } else {
        if (source) {
            emit_expression(w, source);
        } else {
            constant(w, 0, input_line);
            constant(w, 0, line);
            constant(w, 0, column);
            indexed(w, 0x10, "call", target->type == TYPE_REAL ? 24 : 23);
        }
        if (aggregate(target->type)) {
            copy_value(w, destination, type_size(w, target->type));
        } else {
            unsigned type = scalar_type(target->type);
            size_t local = type == 0x7c   ? w->real_local
                           : type == 0x7e ? w->wide_local
                                          : w->left_local;
            indexed(w, 0x21, "local.set", local);
            indexed(w, 0x20, "local.get", destination);
            indexed(w, 0x20, "local.get", local);
            memory_op(w,
                      type == 0x7c   ? 0x39
                      : type == 0x7e ? 0x37
                                     : 0x36,
                      type == 0x7c   ? "f64.store"
                      : type == 0x7e ? "i64.store"
                                     : "i32.store",
                      type == 0x7f ? 2 : 3, 0);
        }
    }
    if (target->type != TYPE_CHARACTER) {
        indexed(w, 0x20, "local.get", destination);
        constant(w, 0, 1);
        memory_op(w, 0x3a, "i32.store8", 0, 8);
    }
    if (target->kind != LV_SYMBOL || w->program->symbols[target->symbol].by_reference) {
        indexed(w, 0x20, "local.get", root);
        constant(w, 0, 1);
        memory_op(w, 0x3a, "i32.store8", 0, 8);
    }
    w->next_local = saved;
}

/* Save the current borrow chain without changing frame ownership. */
static size_t borrow_checkpoint(Wasm *w)
{
    if (!w->heap) {
        return SIZE_MAX;
    }
    size_t checkpoint = temporary(w);
    indexed(w, 0x23, "global.get", 5);
    indexed(w, 0x21, "local.set", checkpoint);
    return checkpoint;
}

/* Release borrows before any of their frame leases can be overwritten. */
static void release_borrows(Wasm *w, size_t checkpoint)
{
    if (w->heap) {
        indexed(w, 0x20, "local.get", checkpoint);
        indexed(w, 0x10, "call", heap_index(w, HEAP_UNPIN));
    }
}

/* An activation owns only the borrow records after its saved entry checkpoint. */
static void release_frame_borrows(Wasm *w)
{
    if (w->heap) {
        indexed(w, 0x20, "local.get", w->frame_local);
        memory_op(w, 0x28, "i32.load", 2, 8);
        indexed(w, 0x10, "call", heap_index(w, HEAP_UNPIN));
    }
}

/* Discard expression results after their scalar/copy consumer has finished.
 * Resetting on every loop test prevents transient results accumulating per iteration. */
static void discard_temporaries(Wasm *w)
{
    release_frame_borrows(w);
    indexed(w, 0x20, "local.get", w->frame_local);
    constant(w, 0, w->routine ? (int64_t)w->layout.frames[w->routine->index] : WASM_FRAME_HEADER);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x24, "global.set", 1);
}

/* Reserve private snapshot/result storage; callers manage its bounded lifetime. */
static size_t reserve_temporary(Wasm *w, size_t size, unsigned line, unsigned column)
{
    size_t destination = temporary(w);
    allocate_storage(w, size, destination, line, column);
    return destination;
}

/* Restore the arena top while leaving any scalar operands beneath it undisturbed. */
static void restore_top(Wasm *w, size_t base, size_t size)
{
    indexed(w, 0x20, "local.get", base);
    if (size) {
        constant(w, 0, (int64_t)size);
        op(w, 0x6a, "i32.add");
    }
    indexed(w, 0x24, "global.set", 1);
}

/* Snapshot each aggregate before later arguments run. Callee frames own separate
 * copies; only a hidden result buffer survives the call's temporary allocations. */
static void emit_call(Wasm *w, const Routine *routine, const Expr *arguments, unsigned line,
                      unsigned column)
{
    size_t saved = w->next_local;
    size_t top = temporary(w), pins = borrow_checkpoint(w);
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x21, "local.set", top);
    if (routine->parent) {
        lexical_frame(w, routine->parent->index);
    }
    int result = routine->is_function && aggregate(routine->return_type);
    size_t output = SIZE_MAX, output_size = 0;
    if (result) {
        output_size = type_size(w, routine->return_type);
        output = reserve_temporary(w, output_size, line, column);
        indexed(w, 0x20, "local.get", output);
    }
    size_t argument_index = 0;
    for (const Expr *argument = arguments; argument; argument = argument->next, ++argument_index) {
        size_t argument_locals = w->next_local;
        size_t argument_pins =
            routine->parameter_modes[argument_index] ? SIZE_MAX : borrow_checkpoint(w);
        if (routine->parameter_modes[argument_index]) {
            size_t target = temporary(w), root = temporary(w);
            emit_path(w, argument->lvalue, root);
            indexed(w, 0x21, "local.set", target);
            check_target(w, target, argument->type, argument->line, argument->column);
            indexed(w, 0x20, "local.get", target);
            indexed(w, 0x20, "local.get", root);
        } else if (argument->type->kind == TYPE_KIND_STRING) {
            emit_expression(w, argument);
            size_t snapshot = snapshot_string(w, argument->line, argument->column);
            release_borrows(w, argument_pins);
            indexed(w, 0x20, "local.get", snapshot);
        } else if (aggregate(argument->type)) {
            size_t size = type_size(w, argument->type);
            size_t snapshot = reserve_temporary(w, size, argument->line, argument->column);
            emit_expression(w, argument);
            copy_value(w, snapshot, size);
            release_borrows(w, argument_pins);
            restore_top(w, snapshot, size);
            indexed(w, 0x20, "local.get", snapshot);
        } else {
            size_t argument_top = temporary(w);
            indexed(w, 0x23, "global.get", 1);
            indexed(w, 0x21, "local.set", argument_top);
            emit_expression(w, argument);
            release_borrows(w, argument_pins);
            restore_top(w, argument_top, 0);
        }
        w->next_local = argument_locals;
    }
    call_routine(w, routine, line, column);
    release_borrows(w, pins);
    restore_top(w, result ? output : top, output_size);
    if (result) {
        indexed(w, 0x20, "local.get", output);
    }
    w->next_local = saved;
}

/* Commit a pointer only after allocation/release succeeds. Captured heap
 * destinations remain borrowed through the operation and share ordinary roots. */
static void pointer_statement(Wasm *w, const Stmt *s)
{
    size_t saved = w->next_local;
    size_t destination = temporary(w), root = temporary(w);
    emit_path(w, s->target, root);
    indexed(w, 0x21, "local.set", destination);
    check_target(w, destination, s->target->type, s->line, s->column);
    if (s->kind == ST_DISPOSE) {
        indexed(w, 0x20, "local.get", destination);
        memory_op(w, 0x2d, "i32.load8_u", 0, 8);
        op(w, 0x45, "i32.eqz");
        check(w, 1, s->line, s->column);
        indexed(w, 0x20, "local.get", destination);
        memory_op(w, 0x29, "i64.load", 3, 0);
    }
    constant(w, 0, (int64_t)type_size(w, s->target->type->element));
    constant(w, 0, s->line);
    constant(w, 0, s->column);
    indexed(w, 0x10, "call", heap_index(w, s->kind == ST_NEW ? HEAP_ALLOCATE : HEAP_RELEASE));
    if (s->kind == ST_DISPOSE) {
        constant(w, 1, 0);
    }
    indexed(w, 0x21, "local.set", w->wide_local);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", w->wide_local);
    memory_op(w, 0x37, "i64.store", 3, 0);
    indexed(w, 0x20, "local.get", destination);
    constant(w, 0, 1);
    memory_op(w, 0x3a, "i32.store8", 0, 8);
    indexed(w, 0x20, "local.get", root);
    constant(w, 0, 1);
    memory_op(w, 0x3a, "i32.store8", 0, 8);
    w->next_local = saved;
}

/* Emit a user call with bounded recursion. Arguments are already on the stack;
 * the counter update leaves them undisturbed and is private to the module. */
static void call_routine(Wasm *w, const Routine *routine, unsigned line, unsigned column)
{
    size_t counter = 0;
    indexed(w, 0x23, "global.get", counter);
    constant(w, 0, P2W_CALL_DEPTH_LIMIT);
    op(w, 0x4f, "i32.ge_u");
    check(w, 5, line, column);
    indexed(w, 0x23, "global.get", counter);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x24, "global.set", counter);
    indexed(w, 0x10, "call", IMPORT_COUNT + 1 + routine->index);
    indexed(w, 0x23, "global.get", counter);
    constant(w, 0, 1);
    op(w, 0x6b, "i32.sub");
    indexed(w, 0x24, "global.set", counter);
}

/* Forward declaration permits counted-loop bodies to contain any statement. */
static void emit_statement(Wasm *w, const Stmt *s);

/* Capture bounds once, protect endpoint stepping, and invalidate the control
 * variable afterward. Nested loops borrow distinct locals; siblings reuse them. */
static void emit_for(Wasm *w, const Stmt *s)
{
    size_t limit = temporary(w), address = temporary(w);
    emit_expression(w, s->expression);
    emit_expression(w, s->limit);
    indexed(w, 0x21, "local.set", limit);
    symbol_address(w, s->symbol);
    indexed(w, 0x21, "local.set", address);
    check_writable(w, address, WASM_CELL_SIZE, s->line, s->column);
    assign(w, s->symbol);
    discard_temporaries(w);
    indexed(w, 0x23, "global.get", 2);
    constant(w, 0, WASM_CONTROL_LIMIT);
    op(w, 0x4f, "i32.ge_u");
    check(w, 7, s->line, s->column);
    constant(w, 0, (int64_t)w->layout.control_base);
    indexed(w, 0x23, "global.get", 2);
    constant(w, 0, 4);
    op(w, 0x6c, "i32.mul");
    op(w, 0x6a, "i32.add");
    indexed(w, 0x20, "local.get", address);
    memory_op(w, 0x36, "i32.store", 2, 0);
    indexed(w, 0x23, "global.get", 2);
    constant(w, 0, 1);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x24, "global.set", 2);
    open_control(w, 0x02, "block");
    open_control(w, 0x03, "loop");
    Loop loop = {w->depth - 2, w->depth, s->symbol, w->loop};
    variable(w, s->symbol, 0, 0);
    indexed(w, 0x20, "local.get", limit);
    op(w, s->descending ? 0x48 : 0x4a, s->descending ? "i32.lt_s" : "i32.gt_s");
    indexed(w, 0x0d, "br_if", 1);
    open_control(w, 0x02, "block");
    w->loop = &loop;
    emit_statement(w, s->body);
    w->loop = loop.outer;
    close_control(w);
    variable(w, s->symbol, 0, 0);
    indexed(w, 0x20, "local.get", limit);
    op(w, 0x46, "i32.eq");
    indexed(w, 0x0d, "br_if", 1);
    variable(w, s->symbol, 0, 0);
    constant(w, 0, s->descending ? -1 : 1);
    op(w, 0x6a, "i32.add");
    variable(w, s->symbol, 0, 1);
    indexed(w, 0x0c, "br", 0);
    close_control(w);
    close_control(w);
    indexed(w, 0x23, "global.get", 2);
    constant(w, 0, 1);
    op(w, 0x6b, "i32.sub");
    indexed(w, 0x24, "global.set", 2);
    constant(w, 0, 0);
    variable(w, s->symbol, 1, 1);
    w->next_local = limit;
}

/* Evaluate the selector once and branch out after the first matching arm.
 * Nested cases and counted loops retain distinct temporaries until their end. */
static void emit_case(Wasm *w, const Stmt *s)
{
    size_t selector = w->next_local++;
    if (w->next_local > w->local_count) {
        w->local_count = w->next_local;
    }
    emit_expression(w, s->expression);
    indexed(w, 0x21, "local.set", selector);
    discard_temporaries(w);
    unsigned done = w->depth;
    open_control(w, 0x02, "block");
    for (const CaseArm *arm = s->arms; arm; arm = arm->next) {
        for (const CaseLabel *label = arm->labels; label; label = label->next) {
            indexed(w, 0x20, "local.get", selector);
            constant(w, 0, label->value);
            if (label->value == label->high) {
                op(w, 0x46, "i32.eq");
            } else {
                op(w, 0x4e, "i32.ge_s");
                indexed(w, 0x20, "local.get", selector);
                constant(w, 0, label->high);
                op(w, 0x4c, "i32.le_s");
                op(w, 0x71, "i32.and");
            }
            if (label != arm->labels) {
                op(w, 0x72, "i32.or");
            }
        }
        open_control(w, 0x04, "if");
        emit_statement(w, arm->body);
        indexed(w, 0x0c, "br", w->depth - done - 1);
        close_control(w);
    }
    emit_statement(w, s->otherwise);
    close_control(w);
    w->next_local = selector;
}

/* Lower structured statements and block lists, preserving source execution order. */
static void emit_statement(Wasm *w, const Stmt *s)
{
    for (; s; s = s->next) {
        switch (s->kind) {
        case ST_EMPTY:
            break;
        case ST_BLOCK:
            emit_statement(w, s->body);
            break;
        case ST_ASSIGN:
            emit_assignment(w, s->target, s->expression, s->line, s->column, 0);
            break;
        case ST_IF:
            emit_expression(w, s->expression);
            discard_temporaries(w);
            open_control(w, 0x04, "if");
            emit_statement(w, s->body);
            if (s->otherwise) {
                --w->indent;
                op(w, 0x05, "else");
                ++w->indent;
                emit_statement(w, s->otherwise);
            }
            close_control(w);
            break;
        case ST_WHILE: {
            open_control(w, 0x02, "block");
            open_control(w, 0x03, "loop");
            Loop loop = {w->depth - 2, w->depth - 1, SIZE_MAX, w->loop};
            emit_expression(w, s->expression);
            discard_temporaries(w);
            op(w, 0x45, "i32.eqz");
            indexed(w, 0x0d, "br_if", 1);
            w->loop = &loop;
            emit_statement(w, s->body);
            w->loop = loop.outer;
            indexed(w, 0x0c, "br", 0);
            close_control(w);
            close_control(w);
            break;
        }
        case ST_REPEAT: {
            open_control(w, 0x02, "block");
            open_control(w, 0x03, "loop");
            Loop loop = {w->depth - 2, w->depth, SIZE_MAX, w->loop};
            open_control(w, 0x02, "block");
            w->loop = &loop;
            emit_statement(w, s->body);
            w->loop = loop.outer;
            close_control(w);
            emit_expression(w, s->expression);
            discard_temporaries(w);
            op(w, 0x45, "i32.eqz");
            indexed(w, 0x0d, "br_if", 0);
            close_control(w);
            close_control(w);
            break;
        }
        case ST_CASE:
            emit_case(w, s);
            break;
        case ST_FOR:
            emit_for(w, s);
            break;
        case ST_BREAK:
        case ST_CONTINUE:
            indexed(w, 0x0c, "br",
                    w->depth - (s->kind == ST_BREAK ? w->loop->stop : w->loop->resume) - 1);
            break;
        case ST_EXIT:
            if (s->expression) {
                Lvalue result = {.kind = LV_SYMBOL,
                                 .type = w->routine->return_type,
                                 .symbol = w->routine->result_symbol};
                emit_assignment(w, &result, s->expression, s->line, s->column, 0);
            }
            for (const Loop *loop = w->loop; loop; loop = loop->outer) {
                if (loop->control != SIZE_MAX) {
                    constant(w, 0, 0);
                    variable(w, loop->control, 1, 1);
                }
            }
            /* The outermost void block leads to shared function-result checks. */
            indexed(w, 0x0c, "br", w->depth - 1);
            break;
        case ST_CALL:
            emit_call(w, s->routine, s->expression, s->line, s->column);
            break;
        case ST_WRITE:
            for (const Expr *e = s->expression; e; e = e->next) {
                if (e->kind == EX_TEXT) {
                    constant(w, 0, (int64_t)w->data.length);
                    constant(w, 0, (int64_t)e->length);
                    buffer_append(&w->data, e->text, e->length);
                    indexed(w, 0x10, "call", 0);
                } else {
                    emit_expression(w, e);
                    if (e->type->kind == TYPE_KIND_STRING || e->type == TYPE_CHARACTER) {
                        constant(w, 0, e->line);
                        constant(w, 0, e->column);
                        indexed(w, 0x10, "call", e->type == TYPE_CHARACTER ? 18 : 14);
                    } else {
                        indexed(w, 0x10, "call",
                                e->type == TYPE_REAL      ? 7
                                : e->type == TYPE_BOOLEAN ? 2
                                                          : 1);
                    }
                }
                discard_temporaries(w);
            }
            if (s->newline) {
                indexed(w, 0x10, "call", 3);
            }
            break;
        case ST_READ:
            emit_assignment(w, s->target, NULL, s->line, s->column, s->input_line);
            break;
        case ST_NEW:
        case ST_DISPOSE:
            pointer_statement(w, s);
            break;
        case ST_DISCARD:
            constant(w, 0, s->line);
            constant(w, 0, s->column);
            indexed(w, 0x10, "call", 5);
            break;
        }
        discard_temporaries(w);
    }
}

/* Emit import signatures plus one signature per program-level routine. */
static void module_header(Buffer *binary, const Program *program, int heap)
{
    const unsigned char magic[] = {0, 'a', 's', 'm', 1, 0, 0, 0};
    buffer_append(binary, magic, sizeof(magic));
    Buffer payload = {0};
    uleb(&payload, ROUTINE_TYPE_BASE + program->routine_count + (heap ? HEAP_HELPER_COUNT : 0));
    for (unsigned i = 0; i < IMPORT_COUNT; ++i) {
        buffer_byte(&payload, 0x60);
        uleb(&payload, strlen(imports[i].parameters));
        for (const char *p = imports[i].parameters; *p; ++p) {
            buffer_byte(&payload, *p == 'd' ? 0x7c : 0x7f);
        }
        buffer_byte(&payload, imports[i].result ? 1 : 0);
        if (imports[i].result) {
            buffer_byte(&payload, imports[i].result);
        }
    }
    buffer_byte(&payload, 0x60); /* Main's void signature. */
    buffer_byte(&payload, 0);
    buffer_byte(&payload, 0);
    for (const Routine *r = program->routines; r; r = r->next) {
        buffer_byte(&payload, 0x60);
        size_t parameters = parameter_slots(r);
        uleb(&payload, parameters);
        signature_parameters(&payload, r, 0);
        int result = r->is_function && !aggregate(r->return_type);
        buffer_byte(&payload, result ? 1 : 0);
        if (result) {
            buffer_byte(&payload, scalar_type(r->return_type));
        }
    }
    for (unsigned i = 0; heap && i < HEAP_HELPER_COUNT; ++i) {
        buffer_byte(&payload, 0x60);
        uleb(&payload, strlen(heap_functions[i].parameters));
        for (const char *p = heap_functions[i].parameters; *p; ++p) {
            buffer_byte(&payload, *p == 'l' ? 0x7e : 0x7f);
        }
        buffer_byte(&payload, heap_functions[i].result ? 1 : 0);
        if (heap_functions[i].result) {
            buffer_byte(&payload, heap_functions[i].result);
        }
    }
    section(binary, 1, &payload);
    uleb(&payload, IMPORT_COUNT);
    for (unsigned i = 0; i < IMPORT_COUNT; ++i) {
        name(&payload, "pas2wasm_v1");
        name(&payload, imports[i].name);
        buffer_byte(&payload, 0);
        uleb(&payload, i);
    }
    section(binary, 2, &payload);
    uleb(&payload, 1 + program->routine_count + (heap ? HEAP_HELPER_COUNT : 0));
    uleb(&payload, RUN_TYPE);
    for (const Routine *r = program->routines; r; r = r->next) {
        uleb(&payload, ROUTINE_TYPE_BASE + r->index);
    }
    for (unsigned i = 0; heap && i < HEAP_HELPER_COUNT; ++i) {
        uleb(&payload, ROUTINE_TYPE_BASE + program->routine_count + i);
    }
    section(binary, 3, &payload);
}

/* Allocate and clear an activation before touching any of its storage. The
 * checked frame end is below 64 MiB, so no intermediate address can wrap i32. */
static void allocate_storage(Wasm *w, size_t size, size_t destination, unsigned line,
                             unsigned column)
{
    constant(w, 0, (int64_t)size);
    allocate_dynamic(w, destination, line, column);
}

/* All callers supply a positive aligned size bounded by program memory. */
static void allocate_dynamic(Wasm *w, size_t destination, unsigned line, unsigned column)
{
    indexed(w, 0x23, "global.get", 1);
    indexed(w, 0x22, "local.tee", destination);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x22, "local.tee", w->right_local);
    constant(w, 0, (int64_t)w->layout.stack_limit);
    op(w, 0x4b, "i32.gt_u");
    check(w, 7, line, column);
    indexed(w, 0x20, "local.get", w->right_local);
    indexed(w, 0x24, "global.set", 1);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x21, "local.set", w->left_local);
    open_control(w, 0x03, "loop");
    indexed(w, 0x20, "local.get", w->left_local);
    constant(w, 0, 0);
    memory_op(w, 0x36, "i32.store", 2, 0);
    indexed(w, 0x20, "local.get", w->left_local);
    constant(w, 0, 4);
    op(w, 0x6a, "i32.add");
    indexed(w, 0x22, "local.tee", w->left_local);
    indexed(w, 0x20, "local.get", w->right_local);
    op(w, 0x49, "i32.lt_u");
    indexed(w, 0x0d, "br_if", 0);
    close_control(w);
}

/* Reserve an actual-length string object in the existing checked activation arena. */
static size_t new_string(Wasm *w, size_t length, unsigned line, unsigned column)
{
    size_t destination = temporary(w);
    indexed(w, 0x20, "local.get", length);
    constant(w, 0, P2W_STRING_LIMIT);
    op(w, 0x4b, "i32.gt_u");
    check(w, 12, line, column);
    indexed(w, 0x20, "local.get", length);
    constant(w, 0, 31); /* Header plus byte payload rounded up to sixteen. */
    op(w, 0x6a, "i32.add");
    constant(w, 0, -16);
    op(w, 0x71, "i32.and");
    allocate_dynamic(w, destination, line, column);
    return destination;
}

/* Keep the value of an earlier operand independent of later calls/alias writes. */
static size_t snapshot_string(Wasm *w, unsigned line, unsigned column)
{
    size_t source = temporary(w), length = temporary(w);
    indexed(w, 0x22, "local.tee", source);
    memory_op(w, 0x28, "i32.load", 2, 0);
    indexed(w, 0x21, "local.set", length);
    size_t destination = new_string(w, length, line, column);
    indexed(w, 0x20, "local.get", destination);
    indexed(w, 0x20, "local.get", length);
    indexed(w, 0x20, "local.get", source);
    constant(w, 0, line);
    constant(w, 0, column);
    indexed(w, 0x10, "call", 9);
    return destination;
}

/* Capture one scalar position before RHS/argument effects. A view stays live until
 * its caller's expression/call lease is released, including forwarded references. */
static void character_view(Wasm *w, const Lvalue *value, size_t root)
{
    size_t saved = w->next_local;
    size_t source = temporary(w), index = temporary(w), view = temporary(w);
    emit_path(w, value->base, root);
    indexed(w, 0x21, "local.set", source);
    emit_expression(w, value->index);
    indexed(w, 0x21, "local.set", index);
    indexed(w, 0x20, "local.get", source);
    indexed(w, 0x20, "local.get", index);
    constant(w, 0, value->line);
    constant(w, 0, value->column);
    indexed(w, 0x10, "call", 15);
    op(w, 0x1a, "drop");
    allocate_storage(w, 16, view, value->line, value->column);
    indexed(w, 0x20, "local.get", view);
    indexed(w, 0x20, "local.get", source);
    memory_op(w, 0x36, "i32.store", 2, 0);
    indexed(w, 0x20, "local.get", view);
    indexed(w, 0x20, "local.get", index);
    memory_op(w, 0x36, "i32.store", 2, 4);
    indexed(w, 0x20, "local.get", view);
    constant(w, 0, INT32_MIN);
    op(w, 0x72, "i32.or");
    w->next_local = saved;
}

/* Strings use immutable value addresses. Literal data and checked frame leases
 * belong to the generated module, never to persistent JavaScript-side storage. */
static void string_expression(Wasm *w, const Expr *e)
{
    size_t saved = w->next_local;
    if (e->kind == EX_TEXT) {
        while (w->data.length % 16) {
            buffer_byte(&w->data, 0);
        }
        constant(w, 0, (int64_t)w->data.length);
        for (unsigned i = 0; i < 16; ++i) {
            unsigned byte = i < 8 ? (unsigned)(e->length >> ((i % 4) * 8)) & 255 : i == 8;
            buffer_byte(&w->data, byte);
        }
        buffer_append(&w->data, e->text, e->length);
        while (w->data.length % 16) {
            buffer_byte(&w->data, 0);
        }
    } else if (e->kind == EX_CHARACTER_STRING) {
        size_t value = temporary(w), destination = temporary(w);
        emit_expression(w, e->left);
        indexed(w, 0x21, "local.set", value);
        allocate_storage(w, 32, destination, e->line, e->column);
        indexed(w, 0x20, "local.get", destination);
        constant(w, 0, 4);
        indexed(w, 0x20, "local.get", value);
        constant(w, 0, e->line);
        constant(w, 0, e->column);
        indexed(w, 0x10, "call", 20);
        indexed(w, 0x20, "local.get", destination);
    } else if (e->kind == EX_SLICE) {
        emit_expression(w, e->left);
        size_t source = snapshot_string(w, e->line, e->column);
        size_t start = temporary(w), count = temporary(w), length = temporary(w);
        emit_expression(w, e->right);
        indexed(w, 0x21, "local.set", start);
        emit_expression(w, e->right->next);
        indexed(w, 0x21, "local.set", count);
        indexed(w, 0x20, "local.get", source);
        indexed(w, 0x20, "local.get", start);
        indexed(w, 0x20, "local.get", count);
        constant(w, 0, e->line);
        constant(w, 0, e->column);
        indexed(w, 0x10, "call", 21);
        indexed(w, 0x21, "local.set", length);
        size_t destination = new_string(w, length, e->line, e->column);
        indexed(w, 0x20, "local.get", destination);
        indexed(w, 0x20, "local.get", length);
        indexed(w, 0x20, "local.get", source);
        indexed(w, 0x20, "local.get", start);
        indexed(w, 0x20, "local.get", count);
        constant(w, 0, e->line);
        constant(w, 0, e->column);
        indexed(w, 0x10, "call", 22);
        indexed(w, 0x20, "local.get", destination);
    } else if (e->kind == EX_STRING_CONVERT) {
        size_t source = temporary(w);
        emit_expression(w, e->left);
        indexed(w, 0x22, "local.tee", source);
        memory_op(w, 0x28, "i32.load", 2, 0);
        constant(w, 0, (int64_t)e->type->capacity);
        op(w, 0x4b, "i32.gt_u");
        check(w, 12, e->line, e->column);
        indexed(w, 0x20, "local.get", source);
    } else if (e->kind == EX_LENGTH) {
        emit_expression(w, e->left);
        constant(w, 0, e->line);
        constant(w, 0, e->column);
        indexed(w, 0x10, "call", 12);
    } else {
        emit_expression(w, e->left);
        size_t left = snapshot_string(w, e->line, e->column), right = temporary(w);
        emit_expression(w, e->right);
        indexed(w, 0x21, "local.set", right);
        if (e->kind == EX_ADD) {
            size_t length = temporary(w);
            indexed(w, 0x20, "local.get", left);
            memory_op(w, 0x28, "i32.load", 2, 0);
            indexed(w, 0x20, "local.get", right);
            memory_op(w, 0x28, "i32.load", 2, 0);
            op(w, 0x6a, "i32.add");
            indexed(w, 0x21, "local.set", length);
            size_t destination = new_string(w, length, e->line, e->column);
            indexed(w, 0x20, "local.get", destination);
            indexed(w, 0x20, "local.get", length);
            indexed(w, 0x20, "local.get", left);
            indexed(w, 0x20, "local.get", right);
            constant(w, 0, e->line);
            constant(w, 0, e->column);
            indexed(w, 0x10, "call", 10);
            indexed(w, 0x20, "local.get", destination);
        } else {
            indexed(w, 0x20, "local.get", left);
            indexed(w, 0x20, "local.get", right);
            constant(w, 0, e->line);
            constant(w, 0, e->column);
            indexed(w, 0x10, "call", 11);
            constant(w, 0, 0);
            switch (e->kind) {
            case EX_EQUAL:
                op(w, 0x46, "i32.eq");
                break;
            case EX_UNEQUAL:
                op(w, 0x47, "i32.ne");
                break;
            case EX_LESS:
                op(w, 0x48, "i32.lt_s");
                break;
            case EX_LEQUAL:
                op(w, 0x4c, "i32.le_s");
                break;
            case EX_GREATER:
                op(w, 0x4a, "i32.gt_s");
                break;
            case EX_GEQUAL:
                op(w, 0x4e, "i32.ge_s");
                break;
            default:
                w->code.failed = 1;
                break;
            }
        }
    }
    w->next_local = saved;
}

/* Lower one function. Parameters are copied into fresh addressable cells, and
 * every normal/early exit converges on result checks and one frame release. */
static void emit_function(Wasm *w, const Routine *routine, Buffer *bodies, Buffer *text)
{
    w->routine = routine;
    int hidden = routine && routine->is_function && aggregate(routine->return_type);
    size_t parameters = routine ? parameter_slots(routine) : 0;
    w->frame_local = parameters;
    w->left_local = parameters + 1;
    w->right_local = parameters + 2;
    w->wide_local = parameters + 3;
    w->real_local = w->wide_local + 1;
    w->next_local = w->local_count = w->real_local + 1;
    w->indent = 2;
    allocate_storage(w, routine ? w->layout.frames[routine->index] : WASM_FRAME_HEADER,
                     w->frame_local, routine ? routine->line : 1, routine ? routine->column : 1);
    indexed(w, 0x20, "local.get", w->frame_local);
    if (routine && routine->parent) {
        indexed(w, 0x20, "local.get", 0);
    } else {
        constant(w, 0, 0);
    }
    memory_op(w, 0x36, "i32.store", 2, 0);
    indexed(w, 0x20, "local.get", w->frame_local);
    indexed(w, 0x23, "global.get", 2);
    memory_op(w, 0x36, "i32.store", 2, 4);
    if (w->heap) {
        indexed(w, 0x20, "local.get", w->frame_local);
        indexed(w, 0x23, "global.get", 5);
        memory_op(w, 0x36, "i32.store", 2, 8);
    }
    size_t hidden_slot = routine && routine->parent ? 1 : 0;
    size_t parameter = hidden + hidden_slot;
    for (size_t i = 0; routine && i < routine->parameter_count; ++i) {
        size_t symbol = routine->first_symbol + i;
        if (routine->parameter_modes[i]) {
            binding_address(w, symbol);
            indexed(w, 0x20, "local.get", parameter++);
            memory_op(w, 0x36, "i32.store", 2, 0);
            binding_address(w, symbol);
            indexed(w, 0x20, "local.get", parameter++);
            memory_op(w, 0x36, "i32.store", 2, 4);
            binding_address(w, symbol);
            constant(w, 0, 1);
            memory_op(w, 0x3a, "i32.store8", 0, 8);
        } else if (aggregate(routine->parameter_types[i])) {
            size_t saved = w->next_local;
            size_t destination = temporary(w);
            symbol_address(w, symbol);
            indexed(w, 0x21, "local.set", destination);
            indexed(w, 0x20, "local.get", parameter++);
            copy_typed(w, destination, routine->parameter_types[i], routine->line, routine->column);
            constant(w, 0, 1);
            variable(w, symbol, 1, 1);
            w->next_local = saved;
        } else {
            indexed(w, 0x20, "local.get", parameter++);
            assign(w, symbol);
        }
    }
    open_control(w, 0x02, "block");
    emit_statement(w, routine ? routine->body : w->program->body);
    close_control(w);
    if (routine && routine->is_function) {
        variable(w, routine->result_symbol, 1, 0);
        op(w, 0x45, "i32.eqz");
        check(w, 6, routine->line, routine->column);
        if (hidden) {
            size_t destination = temporary(w);
            indexed(w, 0x20, "local.get", hidden_slot);
            indexed(w, 0x21, "local.set", destination);
            symbol_address(w, routine->result_symbol);
            copy_typed(w, destination, routine->return_type, routine->line, routine->column);
        } else {
            variable(w, routine->result_symbol, 0, 0);
        }
    }
    release_frame_borrows(w);
    indexed(w, 0x20, "local.get", w->frame_local);
    memory_op(w, 0x28, "i32.load", 2, 4);
    indexed(w, 0x24, "global.set", 2);
    indexed(w, 0x20, "local.get", w->frame_local);
    indexed(w, 0x24, "global.set", 1);
    buffer_byte(&w->code, 0x0b);
    size_t temporaries = w->local_count - w->real_local - 1;
    Buffer body = {0};
    uleb(&body, temporaries ? 4 : 3);
    uleb(&body, 3);
    buffer_byte(&body, 0x7f);
    uleb(&body, 1);
    buffer_byte(&body, 0x7e);
    uleb(&body, 1);
    buffer_byte(&body, 0x7c);
    if (temporaries) {
        uleb(&body, temporaries);
        buffer_byte(&body, 0x7f);
    }
    buffer_append(&body, w->code.data, w->code.length);
    uleb(bodies, body.length);
    buffer_append(bodies, body.data, body.length);
    if (body.failed || w->code.failed) {
        bodies->failed = 1;
    }
    if (w->emit_wat) {
        if (routine) {
            buffer_printf(text, "  (func $r%zu", routine->index);
            if (parameters) {
                buffer_printf(text, " (param");
                signature_parameters(text, routine, 1);
                buffer_printf(text, ")");
            }
            if (routine->is_function && !hidden) {
                buffer_printf(text, " (result");
                signature_type(text, scalar_type(routine->return_type), 1);
                buffer_printf(text, ")");
            }
        } else {
            buffer_printf(text, "  (func (export \"run\")");
        }
        buffer_printf(text, "\n    (local");
        for (size_t i = 0; i < 3; ++i) {
            buffer_printf(text, " i32");
        }
        buffer_printf(text, " i64 f64");
        for (size_t i = 0; i < temporaries; ++i) {
            buffer_printf(text, " i32");
        }
        buffer_printf(text, ")\n");
        buffer_append(text, w->text.data, w->text.length);
        buffer_printf(text, "  )\n");
        if (w->text.failed) {
            text->failed = 1;
        }
    }
    buffer_free(&body);
    buffer_free(&w->code);
    buffer_free(&w->text);
}

int wasm_emit(const Program *program, int emit_wat, CompileResult *result)
{
    Wasm w = {0};
    w.emit_wat = emit_wat;
    w.program = program;
    w.heap = program->pointer_types != NULL;
    if (!wasm_layout_build(program, &w.layout, &result->diagnostic)) {
        wasm_layout_destroy(&w.layout);
        return 0;
    }
    Buffer bodies = {0}, functions = {0}, payload = {0};
    uleb(&bodies, 1 + program->routine_count + (w.heap ? HEAP_HELPER_COUNT : 0));
    emit_function(&w, NULL, &bodies, &functions);
    for (const Routine *routine = program->routines; routine; routine = routine->next) {
        emit_function(&w, routine, &bodies, &functions);
    }
    if (w.heap) {
        heap_emit(&w, &bodies, &functions);
    }
    unsigned pages = (unsigned)((w.layout.stack_limit + 65535) / 65536);
    if (!pages) {
        pages = 1;
    }
    module_header(&result->binary, program, w.heap);
    uleb(&payload, 1);
    uleb(&payload, 1);
    uleb(&payload, pages);
    uleb(&payload, 1024);
    section(&result->binary, 5, &payload);
    uleb(&payload, w.heap ? 6 : 3);
    for (size_t i = 0; i < (w.heap ? 6u : 3u); ++i) {
        buffer_byte(&payload, 0x7f);
        buffer_byte(&payload, 1);
        buffer_byte(&payload, 0x41);
        sleb(&payload, i == 1   ? (int64_t)w.layout.stack_base
                       : i == 3 ? (int64_t)w.layout.stack_limit
                                : 0);
        buffer_byte(&payload, 0x0b);
    }
    section(&result->binary, 6, &payload);
    uleb(&payload, 2);
    name(&payload, "run");
    buffer_byte(&payload, 0);
    uleb(&payload, IMPORT_COUNT);
    name(&payload, "memory");
    buffer_byte(&payload, 2);
    uleb(&payload, 0);
    section(&result->binary, 7, &payload);
    section(&result->binary, 10, &bodies);
    uleb(&payload, 1);
    uleb(&payload, 0);
    buffer_byte(&payload, 0x41);
    uleb(&payload, 0);
    buffer_byte(&payload, 0x0b);
    uleb(&payload, w.data.length);
    buffer_append(&payload, w.data.data, w.data.length);
    section(&result->binary, 11, &payload);
    if (emit_wat) {
        buffer_printf(&result->wat, "(module\n");
        for (unsigned i = 0; i < IMPORT_COUNT; ++i) {
            buffer_printf(&result->wat, "  (import \"pas2wasm_v1\" \"%s\" (func", imports[i].name);
            if (*imports[i].parameters) {
                buffer_printf(&result->wat, " (param");
                for (const char *p = imports[i].parameters; *p; ++p) {
                    signature_type(&result->wat, *p == 'd' ? 0x7c : 0x7f, 1);
                }
                buffer_printf(&result->wat, ")");
            }
            if (imports[i].result) {
                buffer_printf(&result->wat, " (result");
                signature_type(&result->wat, imports[i].result, 1);
                buffer_printf(&result->wat, ")");
            }
            buffer_printf(&result->wat, "))\n");
        }
        buffer_printf(&result->wat, "  (memory (export \"memory\") %u 1024)\n", pages);
        buffer_printf(&result->wat, "  (global (mut i32) (i32.const 0))\n");
        buffer_printf(&result->wat, "  (global (mut i32) (i32.const %zu))\n", w.layout.stack_base);
        buffer_printf(&result->wat, "  (global (mut i32) (i32.const 0))\n");
        if (w.heap) {
            buffer_printf(&result->wat, "  (global (mut i32) (i32.const %zu))\n",
                          w.layout.stack_limit);
            buffer_printf(&result->wat, "  (global (mut i32) (i32.const 0))\n");
            buffer_printf(&result->wat, "  (global (mut i32) (i32.const 0))\n");
        }
        buffer_append(&result->wat, functions.data, functions.length);
        buffer_printf(&result->wat, "  (data (i32.const 0) \"");
        for (size_t i = 0; i < w.data.length; ++i) {
            buffer_printf(&result->wat, "\\%02x", w.data.data[i]);
        }
        buffer_printf(&result->wat, "\")\n)\n");
    }
    int success =
        !w.data.failed && !functions.failed && !result->binary.failed && !result->wat.failed;
    int literals_fit = w.data.length <= P2W_SOURCE_LIMIT;
    buffer_free(&w.data);
    buffer_free(&functions);
    wasm_layout_destroy(&w.layout);
    if (!literals_fit) {
        snprintf(result->diagnostic.code, sizeof(result->diagnostic.code), "E_LAYOUT");
        snprintf(result->diagnostic.message, sizeof(result->diagnostic.message),
                 "Literal data exceeds its bounded memory region.");
        return 0;
    }
    if (!success) {
        snprintf(result->diagnostic.code, sizeof(result->diagnostic.code), "E_MEMORY");
        snprintf(result->diagnostic.message, sizeof(result->diagnostic.message),
                 "Code generation allocation failed.");
    } else {
        result->format = "wasm";
    }
    return success;
}
