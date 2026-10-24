/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
const assert = require('node:assert/strict');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Confirm CRT is emitted on demand and checks the actual imported i32 boundary. */
module.exports = async function verifyKeyboard(compile) {
    await require('./keyboard-runtime.js')(createPas2WasmRuntime);
    const unused=compile('program p; uses crt; begin end.',true);
    assert(!unused.diagnostic);
    assert(!unused.wat.includes('(func $p2w_readkey'),'Unused CRT unit emitted a helper');
    const result=compile('program p;\nuses crt;\nbegin\n writeln(ord(readkey))\nend.',true);
    assert(!result.diagnostic);
    assert(result.wat.includes('(func $p2w_readkey'),'CRT wrapper was not emitted directly');
    assert.deepEqual(WebAssembly.Module.exports(new WebAssembly.Module(result.binary)).map(e=>e.name),['run','memory']);
    await require('./keyboard-runtime.js').verifyScalar(result.binary,createPas2WasmRuntime);
    console.log('CRT emission, scalar validation and mixed keyboard/line cursor boundaries passed.');
};
