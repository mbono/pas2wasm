/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef P2W_BACKEND_H
#define P2W_BACKEND_H
#include "ir/program.h"

/* A statically registered output target. emit borrows the validated program and
 * writes owned artifacts into result, reporting failure through its diagnostic. */
typedef struct {
    const char *name;
    int (*emit)(const Program *program, int emit_wat, CompileResult *result);
} Backend;

/* Find a registered backend; NULL requests the default wasm32 target. */
const Backend *backend_find(const char *target);
/* Emit a complete binary and optionally WAT through the common Wasm lowering. */
int wasm_emit(const Program *program, int emit_wat, CompileResult *result);
#endif
