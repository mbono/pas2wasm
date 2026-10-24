/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Independently assemble private epoch access; production exports remain run/memory. */
module.exports = async function verifyReference(compile, tool, directory) {
    const result = compile('program p; var n:integer; q:^integer; begin q:=@n; q^:=3 end.', true);
    assert(!result.diagnostic);
    assert.deepEqual(WebAssembly.Module.exports(new WebAssembly.Module(result.binary)).map(e => e.name),
        ['run','memory']);
    const wat = result.wat.replace(/\)\s*$/, '(export "epoch" (global 6))\n)');
    const input = path.join(directory,'reference-epoch.wat'), output = input + '.wasm';
    fs.writeFileSync(input,wat); tool('wasm-as',[input,'-o',output]);
    const host = createPas2WasmRuntime('',()=>{});
    const {instance} = await WebAssembly.instantiate(fs.readFileSync(output),{pas2wasm_v1:host.imports});
    host.attach(instance);
    require('./reference-runtime.js').verifyEpoch(instance.exports);
    for (const [name,code,size,input,limit] of [
        ['address_variant_atomic_identity_budget','R_MEMORY',48,'',1],
        ['address_variant_atomic_range','R_RANGE',48,'',65536],
        ['address_variant_atomic_input','R_STRING_CAPACITY',64,'abc\n',65536]
    ]) {
        const fixture = require('./cases.js').find(test=>test.name === name);
        const artifact = compile(fixture.source);
        assert(!artifact.diagnostic,JSON.stringify(artifact.diagnostic));
        await require('./reference-runtime.js').verifyAtomic(artifact.binary,createPas2WasmRuntime,code,size,input,limit);
    }
    console.log('Address registry, frame epochs, and memory-only lifetime boundaries passed.');
};
