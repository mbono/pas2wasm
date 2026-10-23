/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "backend.h"
#include <string.h>

const Backend *backend_find(const char *target)
{
    static const Backend wasm = {"wasm32", wasm_emit};
    return !target || strcmp(target, wasm.name) == 0 ? &wasm : NULL;
}
