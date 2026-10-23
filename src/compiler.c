/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "pas2wasm.h"
#include "backend/backend.h"
#include "version.h"
#include <stdio.h>
#include <stdlib.h>

const char *p2w_version(void)
{
    return P2W_VERSION;
}

CompileResult *p2w_compile(const char *source, size_t length, int emit_wat, const char *target)
{
    CompileResult *result = calloc(1, sizeof(*result));
    if (!result) {
        return NULL;
    }
    const Backend *backend = backend_find(target);
    if (!backend) {
        snprintf(result->diagnostic.code, sizeof(result->diagnostic.code), "E_TARGET");
        snprintf(result->diagnostic.message, sizeof(result->diagnostic.message),
                 "Unsupported output target; MVP supports wasm32.");
        return result;
    }
    if (!source || length > P2W_SOURCE_LIMIT) {
        snprintf(result->diagnostic.code, sizeof(result->diagnostic.code), "E_LIMIT");
        snprintf(result->diagnostic.message, sizeof(result->diagnostic.message),
                 "Source must not exceed 256 KiB.");
        return result;
    }
    Program program = {0};
    if (frontend_parse(&program, source, length, &result->diagnostic)) {
        backend->emit(&program, emit_wat, result);
    }
    program_destroy(&program);
    if (result->diagnostic.code[0]) {
        buffer_free(&result->binary);
        buffer_free(&result->wat);
    } else {
        result->target = backend->name;
    }
    return result;
}

void p2w_free_result(CompileResult *result)
{
    if (!result) {
        return;
    }
    buffer_free(&result->binary);
    buffer_free(&result->wat);
    free(result);
}

const unsigned char *p2w_binary(const CompileResult *result)
{
    return result->binary.data;
}
size_t p2w_binary_size(const CompileResult *result)
{
    return result->binary.length;
}
const char *p2w_wat(const CompileResult *result)
{
    return (const char *)result->wat.data;
}
const char *p2w_code(const CompileResult *result)
{
    return result->diagnostic.code;
}
const char *p2w_message(const CompileResult *result)
{
    return result->diagnostic.message;
}
unsigned p2w_line(const CompileResult *result)
{
    return result->diagnostic.line;
}
unsigned p2w_column(const CompileResult *result)
{
    return result->diagnostic.column;
}
