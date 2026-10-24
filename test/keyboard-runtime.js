/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Exercise raw chunks and mixed cursor ownership without any DOM or Node-only API.
 * Actual browsers run these same checks in addition to their JSPI worker tests. */
async function verifyKeyboardRuntime(createRuntime) {
    /* Keep failures descriptive in Node and browser automation alike. */
    function check(value, message) {if (!value) throw new Error(message);}
    /* Every failed operation retains the source location of its input request. */
    async function fails(action, code) {
        try {await action();}
        catch (error) {
            check(error.code === code,`Expected ${code}, received ${error.code}: ${error.message}`);
            check(error.line === 7 && error.column === 9,'Keyboard failure lost its location'); return;
        }
        throw new Error(`Expected ${code}`);
    }
    /* Script a live provider, retaining kind/prefix and rejecting extra requests. */
    function host(replies, limits = {}) {
        const requests = []; let cursor = 0;
        const runtime = createRuntime('',()=>{},limits,(line,column,kind,prefix)=>{
            requests.push({line,column,kind,prefix});
            if (cursor >= replies.length) throw new Error('Unexpected input request');
            return Promise.resolve(replies[cursor++]);
        });
        const memory=new WebAssembly.Memory({initial:8,maximum:1024});
        runtime.attach({exports:{memory}});
        return {runtime,memory,h:runtime.imports,requests};
    }
    let e = host(['a😀\r\n\u001b']);
    check(await e.h.key_i32(7,9) === 97,'First raw scalar');
    const second = e.h.key_i32(7,9);
    check(!(second instanceof Promise) && second === 0x1f600,'Buffered key suspended or split an astral scalar');
    check(e.h.key_i32(7,9) === 13 && e.h.key_i32(7,9) === 27,'CRLF/escape key normalization');
    check(e.requests.length === 1 && e.requests[0].kind === 'key','Raw chunk was requested more than once');
    e = host(['x42\rZ']);
    check(await e.h.key_i32(7,9) === 120,'Key before numeric input');
    const number = e.h.stream_i32(1,7,9);
    check(!(number instanceof Promise) && number === 42,'Complete queued line was not consumed synchronously');
    e.h.discard_line(7,9);
    check(e.h.key_i32(7,9) === 90 && e.requests.length === 1,'Line discarded later queued keys');
    e = host(['abc','bcD']);
    check(await e.h.key_i32(7,9) === 97,'Prefix seed');
    await e.h.stream_string(262144,8,1,7,9);
    check(e.requests[1].kind === 'line' && e.requests[1].prefix === 'bc','Raw suffix was not preserved as a line prefix');
    const length=new DataView(e.memory.buffer).getUint32(262144,true);
    check(new TextDecoder().decode(new Uint8Array(e.memory.buffer,262160,length)) === 'bcD',
        'Line prefix or newly supplied suffix was lost');
    // Exact cumulative bytes: raw 'abc' (3), one new 'D' and a delimiter (2).
    e = host(['abc','bcD'],{input:5});
    await e.h.key_i32(7,9); await e.h.stream_string(262144,8,1,7,9); e.h.discard_line(7,9);
    check(e.requests.length === 2,'Already-paid prefix was counted twice');
    e = host(['abc','bcD'],{input:4});
    await e.h.key_i32(7,9);
    await fails(()=>e.h.stream_string(262144,8,1,7,9),'R_INPUT_LIMIT');
    e = host(['abc',null]);
    await e.h.key_i32(7,9); await e.h.stream_string(262144,8,1,7,9); e.h.discard_line(7,9);
    await fails(()=>e.h.key_i32(7,9),'R_INPUT_EOF');
    e = host(['é😀'],{input:6});
    check(await e.h.key_i32(7,9) === 233 && e.h.key_i32(7,9) === 0x1f600,'Exact Unicode key allowance');
    e = host(['é😀'],{input:5}); await fails(()=>e.h.key_i32(7,9),'R_INPUT_LIMIT');
    for (const value of ['',42,undefined]) {
        e = host([value]); await fails(()=>e.h.key_i32(7,9),'R_INPUT_INVALID');
    }
    e = host(['\ud800']); await fails(()=>e.h.key_i32(7,9),'R_ENCODING');
    e = host([null]); await fails(()=>e.h.key_i32(7,9),'R_INPUT_EOF');
    e = host(['abc']); await e.h.key_i32(7,9); e.runtime.finish();
    await fails(()=>e.h.key_i32(7,9),'R_INPUT_EOF');
    check(e.requests.length === 1,'Finishing retained typeahead or reopened the provider');
    // A virtual-file EOF cannot consume accepted raw console typeahead.
    const memory = new WebAssembly.Memory({initial:8,maximum:1024});
    // Use a separate attached runtime with an explicit initialized file request.
    let requests = 0;
    const runtime = createRuntime('',()=>{},{},()=>{requests++;return Promise.resolve('abc');});
    runtime.attach({exports:{memory}}); const h=runtime.imports, view=new DataView(memory.buffer);
    await h.key_i32(7,9);
    [1,8,1,0].forEach((v,i)=>view.setUint32(262512+i*4,v,true));
    new Uint8Array(memory.buffer)[262528]=120;
    const controls=263168, packet=controls+32768;
    [0,0,controls,0].forEach((v,i)=>view.setUint32(packet+i*4,v,true));
    h.file_assign(262144,262144,1,0,0,262512,packet,7,9);
    h.file_control(262144,0,7,9);h.file_control(262144,2,7,9);h.file_control(262144,1,7,9);
    const id=h.file_begin(262144,0,7,9);
    await fails(()=>h.file_text_i32(id,0,7,9),'R_FILE_EOF');
    check(h.key_i32(7,9) === 98 && requests === 1,'File EOF consumed or requested console keys');
    return true;
}

/* Test the actual emitted CRT wrapper against deliberately invalid host scalars. */
async function verifyCrtScalarBoundary(binary, createRuntime) {
    for (const value of [-1,0xd800,0xdfff,0x110000,0x7fffffff]) {
        const runtime=createRuntime('',()=>{}), imports={...runtime.imports,key_i32:()=>value};
        const {instance}=await WebAssembly.instantiate(binary,{pas2wasm_v1:imports});runtime.attach(instance);
        let error;try {instance.exports.run();} catch (failure) {error=failure;}
        if (error?.code !== 'R_CHARACTER' || error.line !== 4 || error.column !== 14)
            throw new Error(`CRT wrapper accepted invalid scalar ${value}`);
    }
    for (const value of [0,0x10ffff]) {
        let output='';const runtime=createRuntime('',text=>output+=text);
        const {instance}=await WebAssembly.instantiate(binary,{pas2wasm_v1:{...runtime.imports,key_i32:()=>value}});
        runtime.attach(instance);instance.exports.run();
        if(output !== value+'\n') throw new Error('CRT wrapper rejected a scalar boundary');
    }
}
if (typeof module !== 'undefined' && module.exports) {
    module.exports=verifyKeyboardRuntime;module.exports.verifyScalar=verifyCrtScalarBoundary;
}
