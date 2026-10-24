/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
#ifndef P2W_WASM_CRT_H
#define P2W_WASM_CRT_H
#include "emitter.h"

enum { CRT_KEY_IMPORT = 53 };
/* Private emitted helper follows Pascal routines and any heap helpers. */
size_t crt_index(const Wasm *w);
/* Emit the documented CRT subset directly, keeping binary and optional WAT aligned. */
void crt_emit(Wasm *w, Buffer *bodies, Buffer *functions);
#endif
