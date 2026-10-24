/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
const assert = require('node:assert/strict');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Observe actual generated writes after a terminal failure, before memory disposal. */
module.exports = async function verifyFiles(compile) {
    for (const [name,code,size] of [
        ['file_atomic_eof_variant','R_FILE_EOF',48],
        ['file_atomic_pinned_copy','R_LIFETIME',48],
        ['file_atomic_text_capacity','R_STRING_CAPACITY',64],
        ['file_atomic_character_owner','R_UNINITIALIZED',64],
        ['file_atomic_text_range','R_RANGE',16],
        ['file_atomic_filename','R_FILE_NAME',16]
    ]) {
        const source = require('./cases.js').find(test=>test.name === name).source;
        const result = compile(source);
        assert(!result.diagnostic,JSON.stringify(result.diagnostic));
        await require('./reference-runtime.js').verifyAtomic(result.binary,createPas2WasmRuntime,code,size);
    }
    // A fresh run has empty virtual contents even within this one host session.
    const create = compile("program p; var f:text; begin assign(f,'x'); rewrite(f); write(f,'secret') end.");
    const reopen = compile("program p; var f:text; begin assign(f,'x'); reset(f) end.");
    const first = createPas2WasmRuntime('',()=>{});
    const a = await WebAssembly.instantiate(create.binary,{pas2wasm_v1:first.imports});
    first.attach(a.instance); a.instance.exports.run();
    assert.throws(()=>first.imports.file_query(262144,2,7,9),{code:'R_FILE_STATE'},
        'Successful main completion retained virtual file bindings');
    const second = createPas2WasmRuntime('',()=>{});
    const b = await WebAssembly.instantiate(reopen.binary,{pas2wasm_v1:second.imports});
    second.attach(b.instance);
    assert.throws(()=>b.instance.exports.run(),{code:'R_FILE_NOT_FOUND'});
    console.log('Virtual file snapshots, atomic failures, and fresh execution state passed.');
};
