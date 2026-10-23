/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Independently assemble test-only helper exports; production keeps run/memory only. */
module.exports = async function verifyHeap(compile, tool, directory) {
    const source = 'program demo; type PInt=^integer; var p:PInt; begin end.';
    const result = compile(source, true);
    assert(!result.diagnostic);
    assert.deepEqual(WebAssembly.Module.exports(new WebAssembly.Module(result.binary)).map(e => e.name),
        ['run', 'memory']);
    const extra = ['allocate', 'validate', 'borrow', 'release', 'unpin'].map(name =>
        `(export "${name}" (func $p2w_${name}))`).join('\n') +
        '\n(export "heapTop" (global 3))\n(export "freeHead" (global 4))' +
        '\n(export "pinHead" (global 5))\n(export "frameTop" (global 1))';
    const wat = result.wat.replace(/\)\s*$/, `${extra}\n)`);
    /* Build fresh instances from assembled WAT, using only the production failure host. */
    async function instance(text, suffix) {
        const input = path.join(directory, `heap-${suffix}.wat`), output = input + '.wasm';
        fs.writeFileSync(input, text); tool('wasm-as', [input, '-o', output]);
        const host = createPas2WasmRuntime('', () => {});
        const {instance} = await WebAssembly.instantiate(fs.readFileSync(output), {pas2wasm_v1:host.imports});
        host.attach(instance); return instance.exports;
    }
    /* Helpers return signed i64 BigInts; addresses/generations use their unsigned bits. */
    const address = handle => Number(BigInt.asUintN(32, handle));
    const hasCode = code => error => error.code === code;
    const h = await instance(wat, 'boundaries');
    const heapBase = h.heapTop.value, frameBase = h.frameTop.value;
    const first = h.allocate(64, 3, 2), second = h.allocate(16, 4, 2);
    new Uint8Array(h.memory.buffer, address(first), 64).fill(0xa5);
    h.release(first, 64, 5, 2);
    assert.throws(() => h.validate(first, 64, 6, 2, 0), hasCode('R_DANGLING'));
    const reused = h.allocate(32, 7, 2);
    assert.equal(address(reused), address(first));
    assert.notEqual(reused, first);
    assert(new Uint8Array(h.memory.buffer, address(reused), 32).every(byte => byte === 0));
    assert.equal(h.validate(reused, 32, 8, 2, 0), address(reused));
    assert.throws(() => h.validate(first, 64, 8, 2, 0), hasCode('R_DANGLING'));
    assert.throws(() => h.validate(reused, 16, 8, 2, 0), hasCode('R_POINTER'));
    assert.throws(() => h.release(reused, 16, 8, 2), hasCode('R_DISPOSE'));
    h.borrow(reused, 32, 9, 2);
    assert.notEqual(h.pinHead.value, 0);
    assert.throws(() => h.release(reused, 32, 10, 2), hasCode('R_LIFETIME'));
    h.unpin(0); h.frameTop.value = frameBase;
    assert.equal(h.pinHead.value, 0);
    h.release(reused, 32, 11, 2);
    const header = address(reused) - 32;
    new DataView(h.memory.buffer).setUint32(header + 8, 0xfffffffe, true);
    const lastGeneration = h.allocate(16, 12, 2);
    assert.equal(BigInt.asUintN(64, lastGeneration) >> 32n, 0xffffffffn);
    assert.equal(h.validate(lastGeneration, 16, 13, 2, 0), address(lastGeneration));
    h.release(lastGeneration, 16, 14, 2);
    const freeHead = h.freeHead.value;
    assert.throws(() => h.allocate(16, 15, 2), error =>
        error.code === 'R_MEMORY' && error.line === 15 && error.column === 2);
    assert.equal(h.freeHead.value, freeHead, 'Generation failure must precede unlinking');
    assert.equal(new DataView(h.memory.buffer).getUint32(header + 8, true), 0xffffffff);
    assert.throws(() => h.borrow(0n, 16, 16, 2), hasCode('R_NIL'));
    assert.throws(() => h.release(0n, 16, 16, 2), hasCode('R_DISPOSE'));
    assert.throws(() => h.borrow((1n << 32n) | 1n, 16, 16, 2), hasCode('R_POINTER'));
    assert.throws(() => h.validate(BigInt(address(second)), 16, 16, 2, 0), hasCode('R_POINTER'));
    h.frameTop.value = heapBase - 16;
    h.borrow(second, 16, 17, 2);
    assert.equal(h.frameTop.value, heapBase);
    assert.throws(() => h.borrow(second, 16, 18, 2), hasCode('R_MEMORY'));
    h.unpin(0); h.frameTop.value = frameBase;
    assert.throws(() => h.unpin(16), hasCode('R_POINTER'));
    h.release(second, 16, 19, 2);
    /* Force memory.grow failure below 64 MiB without changing shipped module limits. */
    const limited = wat.replace(/(\(memory \(export "memory"\) )(\d+) 1024\)/,
        (_, prefix, pages) => `${prefix}${pages} ${pages})`);
    const noGrowth = await instance(limited, 'growth-failure');
    const oldTop = noGrowth.heapTop.value, oldBytes = noGrowth.memory.buffer.byteLength;
    assert.throws(() => noGrowth.allocate(1048576, 20, 2), hasCode('R_MEMORY'));
    assert.equal(noGrowth.heapTop.value, oldTop);
    assert.equal(noGrowth.memory.buffer.byteLength, oldBytes);
    assert.equal(noGrowth.freeHead.value, 0);
    /* Check bump arithmetic before touching memory at the absolute budget boundary. */
    noGrowth.heapTop.value = 67108864 - 16;
    assert.throws(() => noGrowth.allocate(16, 21, 2), hasCode('R_MEMORY'));
    assert.equal(noGrowth.heapTop.value, 67108864 - 16);
    console.log('Private heap WAT checks passed: reuse, generation limits, borrows, growth, and bounds.');
};
