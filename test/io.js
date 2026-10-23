/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

'use strict';
const assert = require('node:assert/strict');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Observe atomic writes and the shared cursor independently of compiler lowering. */
module.exports = async function verifyIO() {
    const memory = new WebAssembly.Memory({initial:1, maximum:1024});
    const host = createPas2WasmRuntime('1 bad\né\n', () => {});
    host.attach({exports:{memory}});
    assert.equal(host.imports.stream_i32(0, 1, 1), 1);
    assert.throws(() => host.imports.stream_i32(0, 2, 4),
        e => e.code === 'R_INPUT_INVALID' && e.line === 2 && e.column === 4);
    host.imports.discard_line(1, 1);
    const original = new Uint8Array(memory.buffer).slice(0, 32);
    assert.throws(() => host.imports.stream_string(0, 1, 0, 3, 5),
        e => e.code === 'R_STRING_CAPACITY' && e.line === 3 && e.column === 5);
    assert.deepEqual(new Uint8Array(memory.buffer).slice(0, 32), original);
    host.imports.stream_string(0, 2, 0, 3, 5);
    assert.equal(host.imports.stream_char(0, 3, 6), 10, 'String leaves LF unread');
    assert.throws(() => host.imports.stream_i32(0, 4, 1), e => e.code === 'R_INPUT_EOF');
    for (const invalid of ['\ud800', '\udfff']) {
        const bad = createPas2WasmRuntime(invalid, () => {});
        bad.attach({exports:{memory}});
        const before = new Uint8Array(memory.buffer).slice(0, 32);
        assert.throws(() => bad.imports.stream_string(0, 8, 0, 1, 1), e => e.code === 'R_ENCODING');
        assert.deepEqual(new Uint8Array(memory.buffer).slice(0, 32), before);
    }
    let requests = 0;
    const lines = ['3 4', '', 'tail', null];
    const live = createPas2WasmRuntime('', () => {}, {}, async () => {
        requests++;
        return lines.shift();
    });
    live.attach({exports:{memory}});
    assert.equal(await live.imports.stream_i32(0, 1, 1), 3);
    assert.equal(requests, 1);
    assert.equal(live.imports.stream_i32(0, 1, 1), 4, 'Buffered values remain synchronous');
    live.imports.discard_line(1, 1);
    assert.equal(await live.imports.stream_char(0, 1, 1), 10, 'Empty submissions are lines');
    await live.imports.stream_string(0, 8, 0, 1, 1);
    live.imports.discard_line(1, 1);
    await assert.rejects(live.imports.stream_i32(0, 9, 2), e => e.code === 'R_INPUT_EOF' && e.line === 9);
    assert.throws(() => live.imports.stream_i32(0, 9, 2), e => e.code === 'R_INPUT_EOF');
    assert.equal(requests, 4, 'Confirmed EOF must never request more input');
    const limited = createPas2WasmRuntime('', () => {}, {input:2}, async () => 'é');
    await assert.rejects(limited.imports.stream_i32(0, 1, 1), e => e.code === 'R_INPUT_LIMIT');
    const accumulated = createPas2WasmRuntime('', () => {}, {input:5}, async () => 'é');
    accumulated.attach({exports:{memory}});
    await accumulated.imports.stream_string(0, 8, 0, 1, 1);
    accumulated.imports.discard_line(1, 1);
    await assert.rejects(accumulated.imports.stream_string(0, 8, 0, 2, 1), e => e.code === 'R_INPUT_LIMIT');
    console.log('Shared input cursor, atomic storage, Unicode, provider, EOF, and input limits passed.');
};
