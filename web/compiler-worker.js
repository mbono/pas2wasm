/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* This worker is assembled from bundled loader, wrapper, and this script.
 * init owns a copy of compiler bytes; compile requests carry {id,source,emitWat}.
 * No imports or fetches occur after init, including when recreating workers. */
let compilerReady;
/* Serialize initialization and compilation through the module promise. */
onmessage = async ({data}) => {
    try {
        if (data.type === 'init') {
            compilerReady = createPas2WasmCompiler(Pas2Wasm, {wasmBinary: data.binary});
            const compile = await compilerReady;
            postMessage({type: 'ready', version: compile.version});
            return;
        }
        const compile = await compilerReady;
        const result = compile(data.source, data.emitWat, data.target || 'wasm32');
        postMessage({id: data.id, result}, result.binary ? [result.binary.buffer] : []);
    } catch (error) {
        postMessage({id: data.id, result: {diagnostic: {
            code: 'E_COMPILER', message: error.message, line: 0, column: 0
        }}});
    }
};
