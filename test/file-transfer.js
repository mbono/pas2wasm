/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Native and Wasm compilers still emit identical modules; only their host-supplied
 * file inputs/results differ. Independently assembled WAT must preserve both. */
module.exports = async function verifyFileTransfer(compile, tool, directory, native) {
    require('./file-transfer-runtime.js')(createPas2WasmRuntime);
    const encoder = new TextEncoder();
    function entries(files) {
        return files.map(file => ({name:file.name,data:encoder.encode(file.text.repeat(file.repeat ?? 1))}));
    }
    function wire(files) { return files.map(file=>({name:file.name,data:Array.from(file.data)})); }
    async function execute(binary, test) {
        let output = '';
        const host = createPas2WasmRuntime(test.input || '', text=>output+=text);
        const {instance} = await WebAssembly.instantiate(binary,{pas2wasm_v1:host.imports});
        host.attach(instance);
        const files = entries(test.files);host.loadFiles(files);
        // Importing and rewriting never mutate the caller's seed buffers.
        const original = wire(files);
        let code;
        try { instance.exports.run(); } catch (error) { code=error.code; }
        const results = wire(host.takeFiles());host.finish();
        assert.deepEqual(wire(files),original,'Execution changed supplied bytes');
        return {output,code,files:results};
    }
    for (const fixture of require('./file-transfer.json')) {
        const test = {...fixture, source:fixture.source ?? require('./cases.js').find(c=>c.name===fixture.sourceCase).source};
        const artifact = compile(test.source,true);
        assert(!artifact.diagnostic,JSON.stringify(artifact.diagnostic));
        const source = path.join(directory,`transfer-${test.name}.pas`), output = source+'.wasm';
        fs.writeFileSync(source,test.source);
        tool(native,['--emit-wat','-o',output,source]);
        assert.deepEqual(fs.readFileSync(output),Buffer.from(artifact.binary),test.name+' native bytes');
        const wat = source+'.wat';assert.equal(fs.readFileSync(wat,'utf8'),artifact.wat,test.name+' native WAT');
        const roundtrip=output+'.roundtrip';tool('wasm-as',[wat,'-o',roundtrip]);
        const expected={output:test.output,code:test.runtime,files:wire(entries(test.resultFiles))};
        for (const binary of [artifact.binary,fs.readFileSync(roundtrip),artifact.binary])
            assert.deepEqual(await execute(binary,test),expected,test.name);
    }
    // Node Buffer is a Uint8Array subclass whose slice aliases; seeding must copy it.
    const source = compile("program p; var f:text; c:char; begin assign(f,'x'); reset(f); read(f,c); write(c) end.");
    let output='';const host=createPas2WasmRuntime('',text=>output+=text);
    const {instance}=await WebAssembly.instantiate(source.binary,{pas2wasm_v1:host.imports});host.attach(instance);
    const bytes=Buffer.from('x');host.loadFiles([{name:'x',data:bytes}]);bytes[0]=121;
    instance.exports.run();assert.equal(output,'x');host.finish();
    console.log(`${require('./file-transfer.json').length} file transfer programs passed both hosts/WAT; seed/result ownership and exact budgets passed.`);
};
