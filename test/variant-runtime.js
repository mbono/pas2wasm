/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Exercise the memory-only variant boundary with forged metadata. Self-contained
 * so the same checks run under Node and actual Chromium/Firefox runtimes. */
function verifyVariantRuntime(createRuntime) {
    const memory = new WebAssembly.Memory({initial:8, maximum:1024});
    const view = new DataView(memory.buffer);
    const part = 262160, controls = 262400, request = controls + 32768;
    const lease = request + 64;
    /* Write a u32 field using the documented little-endian ABI. */
    function word(address, value) { view.setUint32(address, value, true); }
    /* Fail with a useful message in either browser automation or Node acceptance. */
    function check(value, message) { if (!value) throw new Error(message); }
    /* Each import failure must retain its stable diagnostic code and location. */
    function fails(action, code) {
        try { action(); }
        catch (error) {
            check(error.code === code, `Expected ${code}, received ${error.code}: ${error.message}`);
            check(error.line === 7 && error.column === 9, 'Variant failure lost its location');
            return;
        }
        throw new Error(`Expected ${code}`);
    }
    /* Attach a fresh runtime, keeping input and immutable-schema caches isolated. */
    function host(input = '') {
        const runtime = createRuntime(input, () => {});
        runtime.attach({exports:{memory}});
        return runtime;
    }
    // Two arms: integer at +16; string[2] at +16, with distinct type identities.
    [2, 1,32,0,32, 16,16,1, 1,48,0,48, 16,32,2].forEach((value,i) => word(i*4,value));
    [1,0,controls,0, part,0,1,0].forEach((value,i) => word(request+i*4,value));
    let runtime = host();
    runtime.imports.variant_access(request,1,7,9);
    check(view.getUint32(part,true) === 1, 'Initial arm was not activated');
    word(part+16,7); view.setUint8(part+24,1);
    const original = new Uint8Array(memory.buffer,part,48).slice();
    /* Every failed plan must leave the complete overlapping storage unchanged. */
    function unchanged() {
        check(original.every((byte,i) => byte === new Uint8Array(memory.buffer)[part+i]),
            'Failed variant request changed existing storage');
    }
    word(request+24,2);
    fails(() => runtime.imports.variant_access(request,0,7,9), 'R_VARIANT'); unchanged();
    [part,0,0,1].forEach((value,i) => word(lease+i*4,value)); word(request+4,lease);
    fails(() => runtime.imports.variant_access(request,1,7,9), 'R_LIFETIME'); unchanged();
    word(controls,part+16); word(request+12,1);
    fails(() => runtime.imports.variant_access(request,1,7,9), 'R_FOR_CONTROL'); unchanged();
    word(request+12,0); word(request+4,0);
    runtime = host('abc\n');
    fails(() => runtime.imports.variant_read_string(part+16,2,1,request,7,9), 'R_STRING_CAPACITY'); unchanged();
    word(request,129);
    fails(() => runtime.imports.variant_access(request,1,7,9), 'R_MEMORY'); unchanged();
    word(request,1); word(request+4,lease); word(lease+4,lease);
    fails(() => runtime.imports.variant_access(request,1,7,9), 'R_MEMORY'); unchanged();
    word(lease+4,0); word(request+4,0);
    word(36,47); // Corrupt the second arm's nested offset; caches never cross attachments.
    runtime = host();
    fails(() => runtime.imports.variant_access(request,1,7,9), 'R_MEMORY'); unchanged();
    word(36,48);
    runtime = host();
    runtime.imports.variant_access(request,1,7,9);
    check(view.getUint32(part,true) === 2, 'Second arm was not activated');
    check(new Uint8Array(memory.buffer,part+16,32).every(byte => byte === 0),
        'Incompatible field initialization was not cleared');
}
/* Test-only Wasm exports expose the real unpin helper, not a replacement. These
 * checks ensure kind-1 leases cannot bypass address/schema/active-arm validation. */
function verifyVariantUnpin(h) {
    const view = new DataView(h.memory.buffer), base = h.frameTop.value;
    const part = 262144;
    /* Reset one complete lease before each call, retaining the current frame floor. */
    function prepare(address = part, schema = 0, active = 1, kind = 1) {
        view.setUint32(0,1,true); view.setUint32(part,active,true);
        [address,0,schema,kind].forEach((value,i) => view.setUint32(base+i*4,value,true));
        h.pinHead.value = base; h.frameTop.value = base + 16;
    }
    /* All forged descriptors must fail before changing the lease head. */
    function rejected(...args) {
        prepare(...args);
        try { h.unpin(0); }
        catch (error) {
            if (error.code === 'R_POINTER' && h.pinHead.value === base) return;
            throw error;
        }
        throw new Error('Invalid variant lease was accepted');
    }
    rejected(0);
    rejected(h.memory.buffer.byteLength-8);
    rejected(part,262144);
    rejected(part,0,0);
    rejected(part,0,2);
    rejected(part,0,1,2);
    prepare(); h.unpin(0);
    if (h.pinHead.value !== 0 || view.getUint32(part,true) !== 1)
        throw new Error('Valid variant lease cleanup changed its storage');
    h.frameTop.value = base;
}
if (typeof module !== 'undefined' && module.exports) {
    module.exports = verifyVariantRuntime;
    module.exports.verifyUnpin = verifyVariantUnpin;
}
