/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Independently construct file metadata/values at the memory-only import boundary.
 * Self-contained so identical ownership/cursor/limit checks run in both browsers. */
function verifyFileRuntime(createRuntime) {
    /* Report useful failures without depending on a Node-only assertion library. */
    function check(value, message) {if (!value) throw new Error(message);}
    /* Terminal file failures retain source locations and never change Pascal bytes. */
    function fails(action, code) {
        try {action();}
        catch (error) {
            check(error.code === code,`Expected ${code}, received ${error.code}: ${error.message}`);
            check(error.line === 7 && error.column === 9,'File failure lost its location'); return;
        }
        throw new Error(`Expected ${code}`);
    }
    /* A new environment represents a fresh program, with independent memory/state. */
    function environment(limits = {}, input = '', provider = null, extraGlobals = 0) {
        const memory = new WebAssembly.Memory({initial:8,maximum:1024}), view = new DataView(memory.buffer);
        const controls = 263168 + extraGlobals, stack = controls + 32768, packet = stack + 256;
        let output = '';
        const runtime = createRuntime(input,text=>output+=text,limits,provider);
        runtime.attach({exports:{memory}});
        /* Little-endian words and bounded UTF-8 objects follow the published ABI. */
        function word(pointer, value) {view.setUint32(pointer,value,true);}
        function text(pointer, value, capacity = 128) {
            const encoded = new TextEncoder().encode(value); check(encoded.length <= capacity,'Test string too large');
            word(pointer,encoded.length); word(pointer+4,capacity); view.setUint8(pointer+8,1);
            new Uint8Array(memory.buffer,pointer+16,encoded.length).set(encoded);
        }
        [0,0,controls,0].forEach((value,i)=>word(packet+i*4,value));
        /* Assign uses a checked global receiver; a filename alone creates no data. */
        function assign(target = 262144, name = 'virtual', kind = 0, type = 1, size = 16) {
            text(262512,name); runtime.imports.file_assign(target,target,kind,type,size,262512,packet,7,9);
        }
        function control(target, operation) {runtime.imports.file_control(target,operation,7,9);}
        function begin(target, mode) {return runtime.imports.file_begin(target,mode,7,9);}
        function end(id) {runtime.imports.file_end(id,7,9);}
        function query(target, operation) {return runtime.imports.file_query(target,operation,7,9);}
        return {memory,view,word,text,assign,control,begin,end,query,packet,stack,
            runtime,h:runtime.imports,output:()=>output};
    }
    let e = environment(); e.assign(); e.control(262144,0);
    let id = e.begin(262144,1);
    e.word(262400,42); e.view.setUint8(262408,1);
    e.h.file_write(id,262400,16,7,9); e.word(262400,99); e.h.file_write(id,262400,16,7,9);
    check(e.query(262144,2) === 2,'Typed filesize does not count elements');
    e.end(id); e.control(262144,2); e.control(262144,1);
    e.assign(262160); e.control(262160,1);
    id = e.begin(262144,0); const second = e.begin(262160,0);
    e.h.file_read(id,262416,16,e.packet,7,9);
    check(e.view.getInt32(262416,true) === 42,'File write retained a mutable source alias');
    e.h.file_read(id,262416,16,e.packet,7,9);
    check(e.view.getInt32(262416,true) === 99 && e.query(262144,0) === 1,'Typed input cursor is incorrect');
    e.h.file_read(second,262416,16,e.packet,7,9);
    check(e.view.getInt32(262416,true) === 42 && e.query(262160,0) === 0,'Readers share one cursor');
    const before = new Uint8Array(e.memory.buffer,262416,16).slice();
    fails(()=>e.h.file_read(id,262416,16,e.packet,7,9),'R_FILE_EOF');
    check(before.every((v,i)=>v === new Uint8Array(e.memory.buffer)[262416+i]),'EOF changed a destination');
    // File input/output uses no console budget and cannot move its input cursor.
    e = environment({input:3,output:1},'7\n'); e.assign(262144,'text',1,0,0); e.control(262144,0);
    id = e.begin(262144,1); e.text(262832,'12 2.5 x\nhello');
    e.h.file_text_write(id,4,262832,0,0,7,9); e.end(id); e.control(262144,2); e.control(262144,1);
    id = e.begin(262144,0);
    check(e.h.file_text_i32(id,0,7,9) === 12,'Text integer parsing');
    check(e.h.file_text_f64(id,0,7,9) === 2.5,'Text real parsing');
    check(e.h.file_text_char(id,0,7,9) === 32 && e.h.file_text_char(id,0,7,9) === 120,'Text scalar cursor');
    check(e.query(262144,1) === 1 && e.query(262144,0) === 0,'LF was skipped by eof/eoln');
    e.h.file_text_discard(id,7,9);
    e.h.file_text_string(id,262976,32,1,e.packet,7,9);
    check(new TextDecoder().decode(new Uint8Array(e.memory.buffer,262992,5)) === 'hello','Text remainder copy');
    check(e.query(262144,0) === 1 && e.query(262144,1) === 1,'Unterminated EOF/eoln');
    check(e.h.read_i32(7,9) === 7 && e.output() === '','File operation consumed console input/output budget');
    e.end(id); e.runtime.finish();
    fails(()=>e.control(262144,1),'R_FILE_STATE');
    // Names are opaque, case-sensitive strings, and binding names do not create files.
    e = environment(); e.assign(262144,'../PC/path');
    fails(()=>e.control(262144,1),'R_FILE_NOT_FOUND');
    for (const name of ['', 'a\0b', 'x'.repeat(256)]) {
        e = environment();
        if (name.length > 128) {
            e.text(262512,name,256);
            fails(()=>e.h.file_assign(262144,262144,0,1,16,262512,e.packet,7,9),'R_FILE_NAME');
        } else fails(()=>e.assign(262144,name),'R_FILE_NAME');
    }
    e = environment(); fails(()=>e.control(262144,0),'R_UNINITIALIZED');
    e = environment(); e.assign(); e.control(262144,0);
    fails(()=>e.control(262144,0),'R_FILE_STATE');
    e = environment(); e.assign(); e.control(262144,0); id = e.begin(262144,1);
    fails(()=>e.control(262144,2),'R_FILE_STATE');
    e = environment(); e.assign(); e.control(262144,0); e.end(e.begin(262144,1));
    e.assign(262160); fails(()=>e.control(262160,1),'R_FILE_STATE');
    e = environment(); e.assign(); e.control(262144,0); e.control(262144,2);
    e.assign(262160,'virtual',0,2,16); fails(()=>e.control(262160,1),'R_FILE_TYPE');
    e = environment(); e.assign(); e.control(262144,0); e.control(262144,2);
    e.assign(262160,'Virtual'); fails(()=>e.control(262160,1),'R_FILE_NOT_FOUND');
    // Retirement closes only the owning activation, preserving forwarded descriptors.
    e = environment(); e.assign(); e.control(262144,0);
    e.word(e.stack+12,1); e.h.frame_enter(e.stack,64,1,7,9);
    e.text(262512,'local'); e.h.file_assign(e.stack+16,e.stack+16,0,1,16,262512,e.packet,7,9);
    e.control(e.stack+16,0); e.h.frame_leave(e.stack,1,7,9);
    check(e.query(262144,2) === 0,'Frame cleanup closed a global binding');
    e.assign(262160,'local'); e.control(262160,1); // Named bytes survived owner retirement.
    e.runtime.finish();
    // Repeated closed reassignment reclaims descriptors rather than leaking bindings.
    e = environment(); for (let i=0;i<300;i++) e.assign();
    e.control(262144,0); e.runtime.finish();
    // An open console provider must never turn a file EOF into a human-input wait.
    let requests = 0;
    e = environment({},'',()=>{requests++;return Promise.resolve('99');});
    e.assign(262144,'empty',1,0,0); e.control(262144,0); e.control(262144,2); e.control(262144,1);
    id = e.begin(262144,0);
    fails(()=>e.h.file_text_i32(id,0,7,9),'R_FILE_EOF');
    check(requests === 0,'Virtual file EOF asked the console provider for input');
    // Prove each exact positive boundary before rejecting its first excess item.
    e = environment();
    for (let i=0;i<32;i++) {e.assign(262144,'name'+i);e.control(262144,0);e.control(262144,2);}
    e.assign(262144,'name32'); fails(()=>e.control(262144,0),'R_FILE_LIMIT');
    e = environment({},'',null,16*40); e.assign();e.control(262144,0);e.control(262144,2);e.control(262144,1);
    for (let i=0;i<31;i++) {e.assign(263168+i*16);e.control(263168+i*16,1);}
    check(e.query(263168+30*16,0) === 1,'32nd reader was not opened');
    e.assign(263168+31*16); fails(()=>e.control(263168+31*16,1),'R_FILE_LIMIT');
    e = environment({},'',null,16*257);
    for (let i=0;i<256;i++) e.assign(263168+i*16);
    check(e.view.getUint8(263168+255*16+8) === 1,'256th descriptor was not assigned');
    fails(()=>e.assign(263168+256*16),'R_FILE_LIMIT');
    e = environment();e.assign();e.control(262144,0);id=e.begin(262144,1);
    e.word(262400,1);e.view.setUint8(262408,1);
    for (let i=0;i<65536;i++) e.h.file_write(id,262400,16,7,9);
    check(e.query(262144,2) === 65536,'Exact 1 MiB typed contents were not retained');
    fails(()=>e.h.file_write(id,262400,16,7,9),'R_FILE_LIMIT');
    e = environment();e.text(262512,'😀'.repeat(63)+'abc',256);
    e.h.file_assign(262144,262144,1,0,0,262512,e.packet,7,9);e.control(262144,0);
    e.runtime.finish();
    e = environment();e.assign();e.control(262144,0);
    for (let i=0;i<8192;i++) id=e.begin(262144,1);
    check(id > 0,'Maximum private binding borrows were not retained');
    fails(()=>e.begin(262144,1),'R_FILE_LIMIT');
    // Copying descriptor bytes cannot forge a second owner/binding coordinate.
    e = environment();e.assign();e.control(262144,0);
    new Uint8Array(e.memory.buffer,262160,16).set(new Uint8Array(e.memory.buffer,262144,16));
    fails(()=>e.query(262160,2),'R_FILE_STATE');
    return true;
}
if (typeof module !== 'undefined' && module.exports) module.exports = verifyFileRuntime;
