/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "ir/program.h"
#include "ir/number.h"
#include <stdio.h>
#include <string.h>

/* Report a constant-evaluation failure at its operator. */
static int failure(const Expr *e, Diagnostic *diagnostic, const char *code, const char *message)
{
    snprintf(diagnostic->code, sizeof(diagnostic->code), "%s", code);
    snprintf(diagnostic->message, sizeof(diagnostic->message), "%s", message);
    diagnostic->line = e->line;
    diagnostic->column = e->column;
    return 0;
}

/* Integer/Boolean operations retain checked signed arithmetic and eager operands. */
static int integer_operation(const Expr *e, int32_t left, int32_t right, ScalarValue *value,
                             Diagnostic *diagnostic)
{
    int64_t result;
    switch (e->kind) {
    case EX_NEGATE:
        result = -(int64_t)left;
        break;
    case EX_NOT:
        result = !left;
        break;
    case EX_ADD:
        result = (int64_t)left + right;
        break;
    case EX_SUBTRACT:
        result = (int64_t)left - right;
        break;
    case EX_MULTIPLY:
        result = (int64_t)left * right;
        break;
    case EX_DIVIDE:
    case EX_MODULO:
        if (!right) {
            return failure(e, diagnostic, "E_CONST_DIV_ZERO", "Constant division by zero.");
        }
        if (left < 0 || right < 0) {
            return failure(e, diagnostic, "E_CONST_DIV_DOMAIN",
                           "Constant div/mod requires nonnegative dividend and positive divisor.");
        }
        result = e->kind == EX_DIVIDE ? left / right : left % right;
        break;
    case EX_AND:
        result = left & right;
        break;
    case EX_OR:
        result = left | right;
        break;
    case EX_EQUAL:
        result = left == right;
        break;
    case EX_UNEQUAL:
        result = left != right;
        break;
    case EX_LESS:
        result = left < right;
        break;
    case EX_LEQUAL:
        result = left <= right;
        break;
    case EX_GREATER:
        result = left > right;
        break;
    case EX_GEQUAL:
        result = left >= right;
        break;
    default:
        return failure(e, diagnostic, "E_CONST", "Expression is not constant.");
    }
    if (result < INT32_MIN || result > INT32_MAX) {
        return failure(e, diagnostic, "E_INTEGER",
                       "Constant expression overflows signed 32-bit integer.");
    }
    value->integer = (int32_t)result;
    return 1;
}

/* Binary64 operations store each intermediate at its specified width. Bitwise
 * rounding/conversion helpers avoid ambient decimal locale or integer-cast traps. */
static int real_operation(const Expr *e, uint64_t left, uint64_t right, ScalarValue *value,
                          Diagnostic *diagnostic)
{
    double a = number_value(left), b = number_value(right);
    volatile double result;
    switch (e->kind) {
    case EX_NEGATE:
        value->real_bits = left ^ (UINT64_C(1) << 63);
        return 1;
    case EX_EQUAL:
        value->integer = a == b;
        return 1;
    case EX_UNEQUAL:
        value->integer = a != b;
        return 1;
    case EX_LESS:
        value->integer = a < b;
        return 1;
    case EX_LEQUAL:
        value->integer = a <= b;
        return 1;
    case EX_GREATER:
        value->integer = a > b;
        return 1;
    case EX_GEQUAL:
        value->integer = a >= b;
        return 1;
    case EX_ADD:
        result = a + b;
        break;
    case EX_SUBTRACT:
        result = a - b;
        break;
    case EX_MULTIPLY:
        result = a * b;
        break;
    case EX_REAL_DIVIDE:
        if (b == 0) {
            return failure(e, diagnostic, "E_CONST_DIV_ZERO", "Constant division by zero.");
        }
        result = a / b;
        break;
    default:
        return failure(e, diagnostic, "E_CONST", "Expression is not constant.");
    }
    value->real_bits = number_bits(result);
    if (!number_finite(value->real_bits)) {
        return failure(e, diagnostic, "E_REAL_RANGE", "Nonfinite constant real result.");
    }
    return 1;
}

/* Bounded recursive folding preserves runtime operand order and signed zeros. */
static int evaluate(Program *program, const Expr *e, ScalarValue *value, Diagnostic *diagnostic,
                    unsigned depth)
{
    *value = (ScalarValue){0};
    if (depth == 128) {
        return failure(e, diagnostic, "E_LIMIT", "Constant expression is too deep.");
    }
    if (e->kind == EX_INTEGER || e->kind == EX_REAL || e->kind == EX_NIL) {
        value->integer = e->integer;
        value->real_bits = e->real_bits;
        return 1;
    }
    if (e->kind == EX_TEXT) {
        value->text = e->text;
        value->length = e->length;
        return 1;
    }
    if (e->kind == EX_LOAD && e->lvalue->kind == LV_CHARACTER &&
        e->lvalue->base->kind == LV_TEMPORARY) {
        ScalarValue source, index;
        if (!evaluate(program, e->lvalue->base->value, &source, diagnostic, depth + 1) ||
            !evaluate(program, e->lvalue->index, &index, diagnostic, depth + 1)) {
            return 0;
        }
        if (index.integer < 1 || (size_t)index.integer > text_length(source.text, source.length)) {
            return failure(e, diagnostic, "E_BOUNDS", "Constant character index is out of bounds.");
        }
        size_t offset = 0;
        for (int32_t i = 0; i < index.integer; ++i) {
            offset += text_decode(source.text + offset, &value->integer);
        }
        return 1;
    }
    ScalarValue left, right = {0};
    if (!e->left) {
        return failure(e, diagnostic, "E_CONST", "Expression is not constant.");
    }
    if (!evaluate(program, e->left, &left, diagnostic, depth + 1)) {
        return 0;
    }
    if (e->right && !evaluate(program, e->right, &right, diagnostic, depth + 1)) {
        return 0;
    }
    if (e->kind == EX_CHARACTER || e->kind == EX_ORDINAL) {
        if (e->kind == EX_CHARACTER && !character_valid(left.integer)) {
            return failure(e, diagnostic, "E_CHARACTER", "Invalid Unicode scalar value.");
        }
        value->integer = left.integer;
        return 1;
    }
    if (e->kind == EX_CHARACTER_STRING) {
        char *text = program_text(program, 5, diagnostic);
        if (!text) {
            diagnostic->line = e->line;
            diagnostic->column = e->column;
            return 0;
        }
        value->length = text_encode(left.integer, text);
        value->text = text;
        return 1;
    }
    if (e->left->type->kind == TYPE_KIND_STRING) {
        if (e->kind == EX_SLICE) {
            ScalarValue count;
            if (!evaluate(program, e->right->next, &count, diagnostic, depth + 1)) {
                return 0;
            }
            size_t characters = text_length(left.text, left.length);
            if (right.integer < 1 || count.integer < 0 ||
                (size_t)(right.integer - 1) > characters ||
                (size_t)count.integer > characters - (size_t)(right.integer - 1)) {
                return failure(e, diagnostic, "E_BOUNDS", "Constant slice is out of bounds.");
            }
            size_t offset = 0;
            int32_t scalar;
            for (int32_t i = 1; i < right.integer; ++i) {
                offset += text_decode(left.text + offset, &scalar);
            }
            value->text = left.text + offset;
            for (int32_t i = 0; i < count.integer; ++i) {
                size_t width = text_decode(left.text + offset, &scalar);
                offset += width;
                value->length += width;
            }
            return 1;
        }
        if (e->kind == EX_LENGTH) {
            value->integer = (int32_t)text_length(left.text, left.length);
            return 1;
        }
        if (e->kind == EX_ADD) {
            size_t length = left.length + right.length;
            if (length > P2W_STRING_LIMIT) {
                return failure(e, diagnostic, "E_STRING_CAPACITY", "Constant string is too long.");
            }
            char *text = program_text(program, length + 1, diagnostic);
            if (!text) {
                diagnostic->line = e->line;
                diagnostic->column = e->column;
                return 0;
            }
            memcpy(text, left.text, left.length);
            memcpy(text + left.length, right.text, right.length);
            value->text = text;
            value->length = length;
            return 1;
        }
        int order = text_compare(left.text, left.length, right.text, right.length);
        switch (e->kind) {
        case EX_EQUAL:
            value->integer = order == 0;
            break;
        case EX_UNEQUAL:
            value->integer = order != 0;
            break;
        case EX_LESS:
            value->integer = order < 0;
            break;
        case EX_LEQUAL:
            value->integer = order <= 0;
            break;
        case EX_GREATER:
            value->integer = order > 0;
            break;
        case EX_GEQUAL:
            value->integer = order >= 0;
            break;
        default:
            return failure(e, diagnostic, "E_CONST", "Expression is not constant.");
        }
        return 1;
    }
    if (e->kind == EX_TO_REAL) {
        value->real_bits = number_bits((double)left.integer);
        return 1;
    }
    if (e->kind == EX_TRUNCATE || e->kind == EX_ROUND) {
        uint64_t bits = number_integral(left.real_bits, e->kind == EX_ROUND);
        double result = number_value(bits);
        if (!number_finite(bits) || result < INT32_MIN || result > INT32_MAX) {
            return failure(e, diagnostic, "E_CONVERSION", "Real conversion exceeds integer range.");
        }
        value->integer = (int32_t)result;
        return 1;
    }
    if (e->left->type == TYPE_REAL) {
        return real_operation(e, left.real_bits, right.real_bits, value, diagnostic);
    }
    return integer_operation(e, left.integer, right.integer, value, diagnostic);
}

int frontend_evaluate(Program *program, const Expr *e, ScalarValue *value, Diagnostic *diagnostic)
{
    return evaluate(program, e, value, diagnostic, 0);
}

int frontend_constant(Program *program, const Expr *e, int32_t *value, Diagnostic *diagnostic)
{
    ScalarValue scalar;
    if (!frontend_evaluate(program, e, &scalar, diagnostic)) {
        return 0;
    }
    if (e->type != TYPE_INTEGER && e->type != TYPE_BOOLEAN) {
        return failure(e, diagnostic, "E_TYPE", "Expected an integer/Boolean constant.");
    }
    *value = scalar.integer;
    return 1;
}
