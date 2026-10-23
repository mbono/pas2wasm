/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "pas2wasm.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Grow geometrically, checking arithmetic before allocation. */
void buffer_append(Buffer *buffer, const void *bytes, size_t length)
{
    if (buffer->failed) {
        return;
    }
    if (length > SIZE_MAX - buffer->length - 1) {
        buffer->failed = 1;
        return;
    }
    size_t needed = buffer->length + length + 1;
    if (needed > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 256;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) {
                capacity = needed;
                break;
            }
            capacity *= 2;
        }
        void *data = realloc(buffer->data, capacity);
        if (!data) {
            buffer->failed = 1;
            return;
        }
        buffer->data = data;
        buffer->capacity = capacity;
    }
    if (length) {
        memcpy(buffer->data + buffer->length, bytes, length);
    }
    buffer->length += length;
    buffer->data[buffer->length] = 0;
}

void buffer_byte(Buffer *buffer, unsigned byte)
{
    unsigned char value = (unsigned char)byte;
    buffer_append(buffer, &value, 1);
}

void buffer_printf(Buffer *buffer, const char *format, ...)
{
    va_list args, copy;
    va_start(args, format);
    va_copy(copy, args);
    int size = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if (size < 0) {
        buffer->failed = 1;
        va_end(args);
        return;
    }
    char *text = malloc((size_t)size + 1);
    if (!text) {
        buffer->failed = 1;
        va_end(args);
        return;
    }
    vsnprintf(text, (size_t)size + 1, format, args);
    va_end(args);
    buffer_append(buffer, text, (size_t)size);
    free(text);
}

void buffer_free(Buffer *buffer)
{
    free(buffer->data);
    memset(buffer, 0, sizeof(*buffer));
}
