/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Exercise opaque identities against independently constructed memory/leases.
 * Self-contained so actual Chromium/Firefox run these same boundary assertions. */
function verifyReferenceRuntime(createRuntime) {
    const memory = new WebAssembly.Memory({initial:8, maximum:1024});
    const view = new DataView(memory.buffer);
    const controls = 262656, stack = controls + 32768, end = stack + 131072;
    const packet = stack + 2048, result = stack + 1024, lease = stack + 512;
    /* Metadata uses little-endian u32 words; no production helper writes test fixtures. */
    function word(address, value) { view.setUint32(address,value,true); }
    /* Throw a useful failure in either engine without Node assertion dependencies. */
    function check(value, message) { if (!value) throw new Error(message); }
    /* Every rejected import retains the operation's source location. */
    function fails(action, code) {
        try {action();}
        catch (error) {
            check(error.code === code, `Expected ${code}, received ${error.code}: ${error.message}`);
            check(error.line === 7 && error.column === 9, 'Address failure lost its location');
            return;
        }
        throw new Error(`Expected ${code}`);
    }
    /* Requests may contain ordinary claims or a schema-ffffffff reference group. */
    function request(claims = [], head = 0) {
        [claims.length,head,controls,0,...claims.flat()].forEach((value,i) => word(packet+i*4,value));
    }
    /* A fresh attachment must start with an empty frame and identity registry. */
    function host(limit = 2) {
        const runtime = createRuntime('',()=>{},{references:limit});
        runtime.attach({exports:{memory}}); request(); return runtime;
    }
    /* Capture/resolve helpers pass the exact public service signatures. */
    function capture(runtime, target, root = target, size = 16, type = 1) {
        return runtime.imports.address_of(target,root,size,type,packet,7,9);
    }
    function resolve(runtime, token, size = 16) {
        runtime.imports.address_view(Number(BigInt.asIntN(32,token)),Number(token >> 32n),size,result,7,9);
        return view.getUint32(result,true);
    }
    let runtime = host();
    const global = capture(runtime,262144), other = capture(runtime,262160);
    check(global !== other && global === capture(runtime,262144), 'Global identity was not interned');
    check(resolve(runtime,global) === 262144, 'Global address did not resolve');
    fails(()=>capture(runtime,262176),'R_MEMORY');
    fails(()=>resolve(runtime,global,32),'R_POINTER');
    fails(()=>runtime.imports.address_view(1,Number(global>>32n),16,result,7,9),'R_POINTER');
    fails(()=>resolve(runtime,999n<<32n|0x80000000n),'R_DANGLING');
    fails(()=>capture(runtime,16),'R_POINTER');
    // Frame forwarding records the actual owner, independent of the callee's frame.
    runtime = host(3); const persistent = capture(runtime,262144);
    word(stack+12,1); runtime.imports.frame_enter(stack,64,1,7,9);
    const local = capture(runtime,stack+16);
    word(stack+80+12,2); runtime.imports.frame_enter(stack+80,32,2,7,9);
    check(capture(runtime,stack+16) === local, 'Forwarded alias changed frame ownership');
    runtime.imports.frame_leave(stack+80,2,7,9);
    check(resolve(runtime,local) === stack+16, 'Callee retirement invalidated its caller');
    runtime.imports.frame_leave(stack,1,7,9);
    fails(()=>resolve(runtime,local),'R_DANGLING');
    word(stack+12,3); runtime.imports.frame_enter(stack,64,3,7,9);
    const reused = capture(runtime,stack+16);
    check(reused !== local, 'Reused frame resurrected an old identity');
    check(resolve(runtime,persistent) === 262144, 'Frame cleanup invalidated a global');
    fails(()=>runtime.imports.frame_leave(stack,1,7,9),'R_MEMORY');
    runtime.imports.frame_leave(stack,3,7,9);
    // Maximum heap generations keep all 32 bits; interiors remain opaque references.
    runtime = host();
    [0,32,0xffffffff,1,1,32,0,0].forEach((value,i)=>word(end+i*4,value));
    [end,0,0,0].forEach((value,i)=>word(lease+i*4,value)); request([],lease);
    const whole = capture(runtime,end+32,end+32,32);
    check((whole >> 32n) === 0xffffffffn && Number(whole & 0xffffffffn) === end+32,
        'Whole heap address lost its allocation identity');
    const interior = capture(runtime,end+48,end+32);
    check(resolve(runtime,interior) === end+48 && view.getUint32(result+12,true) === 0xffffffff,
        'Interior did not retain its full heap generation');
    word(end+12,0); fails(()=>resolve(runtime,interior),'R_DANGLING');
    word(end+8,1); word(end+12,1);
    const fresh = capture(runtime,end+48,end+32);
    check(fresh !== interior, 'Heap reuse resurrected a stale interior');
    // Compatible variant aliases normalize by offset and declared type.
    runtime = host(4);
    [3,1,32,0,32,16,16,1,1,32,0,32,16,16,1,1,32,0,32,16,16,2]
        .forEach((value,i)=>word(i*4,value));
    const part = 262160; word(part,1);
    request([[part,0,1,0]]); const first = capture(runtime,part+16,part);
    request([[part,0,2,0]]); const alias = capture(runtime,part+16,part);
    check(first === alias, 'Compatible variant aliases have different identities');
    word(part,3); request([[Number(first>>32n),0xffffffff,0,0]]);
    fails(()=>runtime.imports.variant_access(packet,0,7,9),'R_VARIANT');
    fails(()=>runtime.imports.variant_bind(packet,end,end,7,9),'R_MEMORY');
    check(view.getUint32(part,true) === 3, 'Failed binding changed a variant arm');
    const bound = runtime.imports.variant_bind(packet,stack+4096,end,7,9);
    check(view.getUint32(part,true) === 1 && Number(bound & 0xffffffffn) === stack+4112 &&
        Number(bound>>32n) === stack+4096, 'Dynamic claims were not activated and pinned');
    // A stale frame-reference group fails before processing its captured view.
    runtime = host(); word(stack+12,4); runtime.imports.frame_enter(stack,64,4,7,9);
    const expired = capture(runtime,stack+16); runtime.imports.frame_leave(stack,4,7,9);
    request([[Number(expired>>32n),0xffffffff,0,0]]);
    fails(()=>runtime.imports.variant_access(packet,1,7,9),'R_DANGLING');
    // Character identity retains its source/index rather than transient descriptor bytes.
    runtime = host(); request(); const string = 262304;
    word(string,2); word(string+4,8); view.setUint8(string+8,1);
    view.setUint8(string+16,97); view.setUint8(string+17,98);
    word(stack+128,string); word(stack+132,2);
    const character = capture(runtime,(stack+128)|0x80000000,string);
    word(stack+128,0); // Original descriptor no longer exists.
    check(runtime.imports.char_load(resolve(runtime,character)|0,7,9) === 98,
        'Character reference retained a temporary descriptor');
    word(string,1);
    fails(()=>runtime.imports.char_load(resolve(runtime,character)|0,7,9),'R_BOUNDS');
    runtime.attach({exports:{memory}});
    fails(()=>resolve(runtime,character),'R_DANGLING');
    for (const invalid of [0,-1,65537,1.5,NaN]) {
        try {createRuntime('',()=>{},{references:invalid});}
        catch (error) {check(error.code === 'R_MEMORY','Invalid identity limit accepted'); continue;}
        throw new Error('Invalid identity limit accepted');
    }
}
/* The private epoch export exists only in independently assembled test modules.
 * Its final u32 epoch is usable once; exhaustion cannot revive any earlier frame. */
function verifyReferenceEpoch(h) {
    h.epoch.value = -2;
    h.run();
    if (h.epoch.value !== -1) throw new Error('Final address frame epoch was not retained');
    try {h.run();}
    catch (error) {
        if (error.code === 'R_MEMORY' && error.line === 1 && error.column === 1 && h.epoch.value === -1)
            return;
        throw error;
    }
    throw new Error('Address frame epoch wrapped');
}
/* Observe the first record after failed capture/assignment/input. These programs
 * print before the failing operation, allowing an independent byte-for-byte snapshot. */
async function verifyReferenceAtomic(binary, createRuntime, code, size = 48, input = '', limit = 65536) {
    let memory, snapshot;
    const host = createRuntime(input, () => {
        snapshot = new Uint8Array(memory.buffer,262144,size).slice();
    }, {references:limit});
    const {instance} = await WebAssembly.instantiate(binary,{pas2wasm_v1:host.imports});
    host.attach(instance); memory = instance.exports.memory;
    let failure;
    try {instance.exports.run();} catch (error) {failure = error;}
    if (failure?.code !== code || !snapshot) throw new Error(`Expected atomic ${code}, received ${failure?.code}`);
    const after = new Uint8Array(memory.buffer,262144,size);
    if (!snapshot.every((byte,i)=>byte === after[i])) throw new Error(`Failed ${code} changed variant storage`);
}
if (typeof module !== 'undefined' && module.exports) {
    module.exports = verifyReferenceRuntime;
    module.exports.verifyEpoch = verifyReferenceEpoch;
    module.exports.verifyAtomic = verifyReferenceAtomic;
}
