/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Independently exercise host seeding/snapshot ownership and limits in Node and
 * real browsers. No DOM, compiler implementation or Node-only API is required. */
function verifyFileTransferRuntime(createRuntime) {
    function check(value, message) { if (!value) throw new Error(message); }
    function fails(action, code) {
        let error; try { action(); } catch (failure) { error = failure; }
        check(error?.code === code, `Expected ${code}, received ${error?.code}: ${error?.message}`);
    }
    /* File packets use only the published global-storage ABI. */
    function host() {
        const runtime = createRuntime('', () => {}, {input:0,output:0});
        const memory = new WebAssembly.Memory({initial:8,maximum:1024});
        runtime.attach({exports:{memory}});
        const view = new DataView(memory.buffer), packet = 296192;
        [0,0,263168,0].forEach((value,i) => view.setUint32(packet+i*4,value,true));
        function assign(name) {
            const bytes = new TextEncoder().encode(name);
            view.setUint32(262512,bytes.length,true); view.setUint32(262516,256,true); view.setUint8(262520,1);
            new Uint8Array(memory.buffer,262528,bytes.length).set(bytes);
            runtime.imports.file_assign(262144,262144,1,0,0,262512,packet,7,9);
        }
        return {runtime,assign,h:runtime.imports,memory};
    }
    const bytes = text => new TextEncoder().encode(text);
    const entry = (name,text='') => ({name,data:bytes(text)});
    for (const [files,code] of [
        [null,'R_FILE_TYPE'], [[{name:'x',data:[]}],'R_FILE_TYPE'],
        [[entry('')],'R_FILE_NAME'], [[entry('a\0b')],'R_FILE_NAME'],
        [[entry('\ud800')],'R_FILE_NAME'], [[entry('a'.repeat(256))],'R_FILE_NAME'],
        [[entry('é'.repeat(128))],'R_FILE_NAME'], [[entry('x'),entry('x')],'R_FILE_NAME'],
        [Array.from({length:33},(_,i)=>entry('f'+i)),'R_FILE_LIMIT'],
        [[{name:'x',data:new Uint8Array(1048577)}],'R_FILE_LIMIT'],
        [[{name:'x',data:new Uint8Array([0xff])}],'R_ENCODING'],
        [[entry('valid'),{name:'bad',data:new Uint8Array([0xc0,0x80])}],'R_ENCODING']
    ]) {
        const e=host(); fails(()=>e.runtime.loadFiles(files),code);
        check(e.runtime.takeFiles().length===0,'Invalid inputs published results');
        e.assign('valid');fails(()=>e.h.file_control(262144,1,7,9),'R_FILE_NOT_FOUND');
    }
    let e=host();e.runtime.loadFiles([entry('é'.repeat(127)+'a'),entry('A'),entry('a')]);
    e.h.file_finish();check(e.runtime.takeFiles().length===0,'Untouched inputs were exported');
    e=host();const original=entry('x','é😀');e.runtime.loadFiles([original]);original.data.fill(65);
    e.assign('x');e.h.file_control(262144,1,7,9);let id=e.h.file_begin(262144,0,7,9);
    check(e.h.file_text_char(id,0,7,9)===233,'Input retained a mutable caller alias');
    check(e.h.file_text_char(id,0,7,9)===0x1f600,'Astral input bytes changed');
    e.h.file_end(id,7,9);e.h.file_control(262144,2,7,9);
    e.h.file_control(262144,0,7,9);id=e.h.file_begin(262144,1,7,9);
    e.h.file_text_write(id,3,8,0,0,7,9);e.h.file_end(id,7,9);e.h.file_finish();
    const result=e.runtime.takeFiles();
    check(result.length===1&&result[0].name==='x'&&result[0].data[0]===8,'Snapshot changed exact bytes');
    check(e.runtime.takeFiles().length===0,'Result consumption was not one-shot');
    e.runtime.finish();check(result[0].data[0]===8,'Cleanup changed a caller-owned snapshot');
    fails(()=>e.h.file_control(262144,1,7,9),'R_FILE_STATE');
    e=host();e.runtime.loadFiles([]);fails(()=>e.runtime.loadFiles([]),'R_FILE_STATE');
    e=host();e.assign('x');fails(()=>e.runtime.loadFiles([]),'R_FILE_STATE');
    e=host();e.runtime.loadFiles(Array.from({length:32},(_,i)=>entry('f'+i)));
    e.assign('new');fails(()=>e.h.file_control(262144,0,7,9),'R_FILE_LIMIT');
    e=host();e.runtime.loadFiles([{name:'full',data:bytes('a'.repeat(1048576))}]);
    e.assign('out');e.h.file_control(262144,0,7,9);e.h.file_finish();
    check(e.runtime.takeFiles()[0].data.length===0,'Exact input limit prevented empty output');
    e=host();e.runtime.loadFiles([{name:'full',data:bytes('a'.repeat(1048576))}]);
    e.assign('out');e.h.file_control(262144,0,7,9);id=e.h.file_begin(262144,1,7,9);
    fails(()=>e.h.file_text_write(id,3,120,0,0,7,9),'R_FILE_LIMIT');
    check(e.runtime.takeFiles().length===0,'Failed run published partial results');
    e=host();e.assign('x');e.h.file_control(262144,0,7,9);e.h.file_finish();e.runtime.finish();
    check(e.runtime.takeFiles().length===0,'finish retained results');
    e=host();e.assign('x');e.h.file_control(262144,0,7,9);e.h.file_finish();
    e.runtime.attach({exports:{memory:new WebAssembly.Memory({initial:8,maximum:1024})}});
    check(e.runtime.takeFiles().length===0,'Attachment retained old results');e.runtime.loadFiles([]);
    return true;
}
if (typeof module !== 'undefined' && module.exports) module.exports = verifyFileTransferRuntime;
