/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

#include "pas2wasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read a source file with a bounded buffer; close the stream on every path. */
static int read_source(const char *path, Buffer *source)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        perror(path);
        return 0;
    }
    unsigned char bytes[4096];
    size_t count;
    while ((count = fread(bytes, 1, sizeof(bytes), file)) != 0) {
        if (source->length + count > P2W_SOURCE_LIMIT) {
            /* Let the common compiler API report the same limit diagnostic on
             * both hosts, without reading the remainder of an oversized file. */
            buffer_append(source, bytes, P2W_SOURCE_LIMIT + 1 - source->length);
            fclose(file);
            return !source->failed;
        }
        buffer_append(source, bytes, count);
        if (source->failed) {
            fclose(file);
            return 0;
        }
    }
    int success = !ferror(file);
    if (!source->data) {
        buffer_append(source, "", 0);
    }
    fclose(file);
    return success;
}

/* Write one complete artifact, reporting open/write/close failures. */
static int write_file(const char *path, const void *data, size_t length)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return 0;
    }
    int success = fwrite(data, 1, length, file) == length;
    if (fclose(file) != 0) {
        success = 0;
    }
    if (!success) {
        fprintf(stderr, "Could not write %s.\n", path);
    }
    return success;
}

/* Explain the deliberately small CLI without exposing browser runtime details. */
static void usage(void)
{
    puts("Pas2Wasm — Pascal to WebAssembly\n"
         "Usage: pas2wasm [--target=wasm32] [--emit-wat] [-o program.wasm] source.pas\n"
         "       pas2wasm --help | --version\n"
         "WAT is saved beside the binary using the same basename.\n"
         "--diagnostics-json writes a structured diagnostic to stderr on failure.");
}

/* Adapt filesystem arguments to the memory-only compiler core. */
int main(int argc, char **argv)
{
    const char *input = NULL, *output = "program.wasm", *target = NULL;
    int emit_wat = 0, json = 0;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--help") == 0) {
            usage();
            return 0;
        }
        if (strcmp(argv[i], "--version") == 0) {
            printf("Pas2Wasm %s\n", p2w_version());
            return 0;
        }
        if (strcmp(argv[i], "--emit-wat") == 0) {
            emit_wat = 1;
        } else if (strcmp(argv[i], "--diagnostics-json") == 0) {
            json = 1;
        } else if (strncmp(argv[i], "--target=", 9) == 0) {
            target = argv[i] + 9;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (argv[i][0] == '-' || input) {
            usage();
            return 2;
        } else {
            input = argv[i];
        }
    }
    if (!input) {
        usage();
        return 2;
    }
    Buffer source = {0};
    if (!read_source(input, &source)) {
        buffer_free(&source);
        return 1;
    }
    CompileResult *result = p2w_compile((const char *)source.data, source.length, emit_wat, target);
    buffer_free(&source);
    if (!result) {
        fputs("E_MEMORY: Compiler allocation failed.\n", stderr);
        return 1;
    }
    int status = 1;
    if (result->diagnostic.code[0]) {
        Diagnostic *d = &result->diagnostic;
        if (json) {
            fprintf(stderr, "{\"code\":\"%s\",\"message\":\"%s\",\"line\":%u,\"column\":%u}\n",
                    d->code, d->message, d->line, d->column);
        } else {
            fprintf(stderr, "%s:%u:%u: %s: %s\n", input, d->line, d->column, d->code, d->message);
        }
    } else if (strcmp(input, output) == 0) {
        fputs("Refusing to overwrite the source file.\n", stderr);
    } else if (write_file(output, result->binary.data, result->binary.length)) {
        status = 0;
        if (emit_wat) {
            size_t length = strlen(output);
            char *path = malloc(length + 5);
            if (!path) {
                status = 1;
            } else {
                strcpy(path, output);
                char *dot = strrchr(path, '.');
                char *slash = strrchr(path, '/');
                if (dot && (!slash || dot > slash)) {
                    *dot = 0;
                }
                strcat(path, ".wat");
                if (strcmp(path, input) == 0 || strcmp(path, output) == 0) {
                    fputs("WAT path conflicts with source or binary output.\n", stderr);
                    status = 1;
                } else if (!write_file(path, result->wat.data, result->wat.length)) {
                    status = 1;
                }
                free(path);
            }
        }
    }
    p2w_free_result(result);
    return status;
}
