/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#ifndef PAS2WASM_H
#define PAS2WASM_H

#include <stddef.h>
#include <stdint.h>

#define P2W_SOURCE_LIMIT (256u * 1024u)

/* A located diagnostic; code is stable and message is intended for users.
 * Locations use 1-based lines and UTF-8 byte columns. Zero means no location. */
typedef struct {
    char code[32];
    char message[192];
    unsigned line;
    unsigned column;
} Diagnostic;

/* Owned byte storage. A trailing zero is maintained outside length for text.
 * failed records allocation failure; callers release data with buffer_free. */
typedef struct {
    unsigned char *data;
    size_t length;
    size_t capacity;
    int failed;
} Buffer;

/* Compilation owns both artifacts until p2w_free_result. Empty code means success.
 * The only MVP target is wasm32; WAT is absent unless explicitly requested. */
typedef struct {
    Buffer binary;
    Buffer wat;
    Diagnostic diagnostic;
    const char *target; /* Static target identifier; NULL on failure. */
    const char *format; /* Static primary artifact format; NULL on failure. */
} CompileResult;

/* Append bytes, maintaining a trailing zero; allocation failure sets failed. */
void buffer_append(Buffer *buffer, const void *bytes, size_t length);
/* Append one byte. */
void buffer_byte(Buffer *buffer, unsigned byte);
/* Append formatted text; failures are recorded in the buffer. */
void buffer_printf(Buffer *buffer, const char *format, ...);
/* Release owned storage and reset all fields. */
void buffer_free(Buffer *buffer);

/* Return the embedded compiler version. Static storage; do not free it. */
const char *p2w_version(void);

/* Compile borrowed source bytes. A NULL target means wasm32. The caller owns the
 * result, including failed results; NULL indicates failure to allocate a result. */
CompileResult *p2w_compile(const char *source, size_t length, int emit_wat, const char *target);
/* Release a result and its artifacts; accepts NULL. */
void p2w_free_result(CompileResult *result);
/* Artifact/diagnostic accessors borrow storage until p2w_free_result. */
const unsigned char *p2w_binary(const CompileResult *result);
/* Return the binary artifact byte count. */
size_t p2w_binary_size(const CompileResult *result);
/* Return optional UTF-8 WAT, or NULL when not emitted. */
const char *p2w_wat(const CompileResult *result);
/* Return stable diagnostic code, empty on success. */
const char *p2w_code(const CompileResult *result);
/* Return diagnostic message, empty on success. */
const char *p2w_message(const CompileResult *result);
/* Return diagnostic line. */
unsigned p2w_line(const CompileResult *result);
/* Return diagnostic byte column. */
unsigned p2w_column(const CompileResult *result);

#endif
