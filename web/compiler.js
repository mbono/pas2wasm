/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Wrap the memory-only C API. Returned JS artifacts are copied before C cleanup.
 * The factory argument is the generated Pas2Wasm Emscripten module factory. */
async function createPas2WasmCompiler(factory, options = {}) {
    const module = await factory(options);
    const compile = module.cwrap('p2w_compile', 'number', ['number', 'number', 'number', 'string']);
    const destroy = module.cwrap('p2w_free_result', null, ['number']);
    const binary = module.cwrap('p2w_binary', 'number', ['number']);
    const size = module.cwrap('p2w_binary_size', 'number', ['number']);
    const wat = module.cwrap('p2w_wat', 'number', ['number']);
    const code = module.cwrap('p2w_code', 'string', ['number']);
    const message = module.cwrap('p2w_message', 'string', ['number']);
    const line = module.cwrap('p2w_line', 'number', ['number']);
    const column = module.cwrap('p2w_column', 'number', ['number']);
    /* Compile borrowed text; return owned bytes/text or a structured diagnostic.
     * emitWat defaults false and target defaults wasm32. */
    function compileSource(source, emitWat = false, target = 'wasm32') {
        const bytes = new TextEncoder().encode(source);
        if (bytes.length > 256 * 1024) return {diagnostic: {
            code: 'E_LIMIT', message: 'Source must not exceed 256 KiB.', line: 0, column: 0
        }};
        const pointer = module._malloc(bytes.length + 1);
        if (!pointer) throw new Error('Compiler source allocation failed.');
        let result = 0;
        try {
            module.HEAPU8.set(bytes, pointer);
            module.HEAPU8[pointer + bytes.length] = 0;
            result = compile(pointer, bytes.length, emitWat ? 1 : 0, target);
            if (!result) throw new Error('Compiler result allocation failed.');
            if (code(result)) return {diagnostic: {
                code: code(result), message: message(result), line: line(result), column: column(result)
            }};
            const address = binary(result);
            const text = wat(result);
            return {target: 'wasm32', format: 'wasm',
                binary: module.HEAPU8.slice(address, address + size(result)),
                wat: text ? module.UTF8ToString(text) : null};
        } finally {
            if (result) destroy(result);
            module._free(pointer);
        }
    }
    /* Read the loaded Wasm build's identity once; it never needs a runtime file. */
    Object.defineProperty(compileSource, 'version', {
        value: module.cwrap('p2w_version', 'string', [])()
    });
    return compileSource;
}
if (typeof module !== 'undefined' && module.exports) module.exports = createPas2WasmCompiler;
