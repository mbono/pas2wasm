/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Real-browser public API checks. Instrument native worker/URL ownership, not a
 * simulated compiler; isolated faults exercise teardown and recovery contracts. */
async function verifyPas2WasmEngine(base, expectedVersion, transfers, programs) {
    const NativeWorker = window.Worker, nativeFetch = window.fetch;
    const createUrl = URL.createObjectURL, revokeUrl = URL.revokeObjectURL;
    const workerSource = window.pas2WasmWorkerSource;
    const workers = new Set(), urls = new Set(), requests = [], engines = [];
    let fault = '', peakWorkers = 0, checks = 0;
    const metrics = {compileMs:[], startupMs:[], executionMs:[], warmWorkerCompileMs:[], programBytes:{}};
    /* Fail with a useful assertion while retaining the first broken contract. */
    function check(condition, message) { checks++; if (!condition) throw Error(message); }
    /* Assert expected promise rejections without suppressing unexpected failures. */
    async function rejects(promise, key, expected) {
        let caught;
        try { await promise; } catch (error) { caught = error; }
        check(caught?.[key] === expected, `Expected ${key}=${expected}, got ${caught?.stack}`);
    }
    /* Test-only bounded delay for asynchronous/stale delivery and wait budgets. */
    function delay(ms) { return new Promise(resolve => setTimeout(resolve, ms)); }
    window.Worker = class extends NativeWorker {
        constructor(url, options) {
            if (fault === 'startup') throw Error('Injected worker startup failure');
            let injected;
            if (fault === 'hang') injected = createUrl.call(URL, new Blob(['onmessage=()=>{};'], {type:'text/javascript'}));
            try { super(injected || url, options); }
            finally { if (injected) revokeUrl.call(URL, injected); }
            workers.add(this); peakWorkers = Math.max(peakWorkers, workers.size);
        }
        postMessage(data, ...rest) { this.kind = data.type; this.runId = data.id; super.postMessage(data, ...rest); }
        terminate() { workers.delete(this); super.terminate(); }
    };
    URL.createObjectURL = function(blob) { const url = createUrl.call(URL, blob); urls.add(url); return url; };
    URL.revokeObjectURL = function(url) { urls.delete(url); revokeUrl.call(URL, url); };
    window.fetch = function(url, options) {
        requests.push({url:String(url), method:options?.method || 'GET', body:options?.body});
        return nativeFetch.call(window, url, options);
    };
    /* Retain engines for unconditional disposal even on a failed assertion. */
    async function create() { const engine = await Pas2WasmEngine.create({assetBaseUrl:base}); engines.push(engine); return engine; }
    try {
        const started = performance.now(), engine = await create();
        metrics.initializationMs = performance.now() - started;
        check(engine.version === expectedVersion, 'Version must come from loaded compiler');
        check(workers.size === 0 && urls.size === 0, 'Initialization must dispose its probe');
        check(requests.length === 3 && requests.every(r => r.method === 'GET' && !r.body && r.url.startsWith(base)),
            'Only three same-deployment assets may load');
        /* Compile representative programs and retain first/repeated cold-memory timings. */
        async function compile(source, options) {
            const start = performance.now(), result = await engine.compile(source, options);
            metrics.compileMs.push(performance.now() - start);
            check(!result.diagnostic, 'Unexpected compile failure: ' + JSON.stringify(result));
            return result;
        }
        const hello = "program p; begin writeln('Hello, world') end.";
        const plain = await compile(hello), text = await compile(hello, {emitWat:true});
        check(plain.wat === null && text.wat.startsWith('(module'), 'WAT must be opt-in');
        check(String(plain.binary) === String(text.binary) && plain.target === 'wasm32' && plain.format === 'wasm', 'Artifact contract');
        const bad = "program p; begin writeln('é', missing) end.";
        const diagnostic = (await engine.compile(bad)).diagnostic;
        check(diagnostic.code === 'E_UNKNOWN' && diagnostic.line === 1 &&
            diagnostic.column === new TextEncoder().encode(bad.slice(0, bad.indexOf('missing'))).length + 1,
            'Compiler positions must retain UTF-8 byte columns');
        check((await engine.compile(' '.repeat(262145))).diagnostic.code === 'E_LIMIT', 'Source bound');
        check((await engine.compile(hello, {target:'x86_64'})).diagnostic.code === 'E_TARGET', 'Target validation');
        check((await engine.compile(hello, {target:''})).diagnostic.code === 'E_TARGET', 'Empty target validation');
        check((await engine.compile(hello, {target:'wasm32\0suffix'})).diagnostic.code === 'E_TARGET', 'NUL target validation');
        await rejects(engine.compile(7), 'name', 'TypeError');
        const queued = await Promise.all([engine.compile(hello), engine.compile(hello, {emitWat:true})]);
        check(!queued[0].wat && queued[1].wat, 'Concurrent compilation must serialize correctly');

        /* Cancellation covers active and queued jobs, preserves stale-reply guards,
         * and cannot poison the next successful request. */
        fault = 'hang';
        const first = new AbortController(), second = new AbortController();
        const a = engine.compile(hello, {signal:first.signal});
        const abandoned = [...workers][0].onmessage;
        const b = engine.compile(hello, {signal:second.signal});
        const ar = rejects(a, 'name', 'AbortError'), br = rejects(b, 'name', 'AbortError');
        second.abort(); first.abort(); fault = '';
        await Promise.all([ar, br]);
        abandoned({data:{id:1, result:{binary:new Uint8Array([99])}}});
        await compile(hello);
        const preaborted = new AbortController(); preaborted.abort();
        await rejects(engine.compile(hello, {signal:preaborted.signal}), 'name', 'AbortError');
        fault = 'startup'; await rejects(engine.compile(hello), 'code', 'ENGINE_WORKER'); fault = '';
        fault = 'hang'; await rejects(engine.compile(hello), 'code', 'ENGINE_TIMEOUT'); fault = '';
        await compile(hello);

        /* Collect raw output and terminal state ordering using only public calls. */
        async function execute(binary, options = {}) {
            let output = '', returned = false, firstOutput = true;
            const states = [], start = performance.now();
            const session = engine.run(binary, {...options,
                onOutput(text) {
                    if (firstOutput && options.measureStartup) metrics.startupMs.push(performance.now() - start);
                    firstOutput = false; output += text; options.onOutput?.(text);
                },
                onStateChange(state) { check(returned, 'Callbacks must follow run return'); states.push(state); options.onStateChange?.(state); }
            });
            returned = true;
            const outcome = await session.done;
            metrics.executionMs.push(performance.now() - start);
            check(states.at(-1) === outcome.status, 'Terminal state must precede done');
            check(!session.submit('late'), 'Finished sessions cannot accept input');
            check(states.filter(s => ['completed','failed','stopped'].includes(s)).length === 1, 'Exactly one terminal event');
            return {outcome, output, states};
        }
        metrics.pageHeapBeforeRunsBytes = performance.memory?.usedJSHeapSize ?? null;
        for (let i = 0; i < 8; i++) {
            const result = await execute(plain.binary, {measureStartup:true});
            check(result.output === 'Hello, world\n' && result.outcome.status === 'completed', 'Repeated fresh runs');
            check(workers.size === 0 && urls.size === 0, 'Completed run retained a worker or URL');
        }
        metrics.pageHeapAfterRunsBytes = performance.memory?.usedJSHeapSize ?? null;
        const inputProgram = await compile('program p; var n,m:integer; begin readln(n,m); writeln(n+m) end.');
        check((await execute(inputProgram.binary, {input:'2 3\n'})).output === '5\n', 'Batch multi-field readln');
        const runtime = await compile('program p; var n:integer; begin writeln(n) end.');
        const failed = (await execute(runtime.binary)).outcome;
        check(failed.diagnostic.code === 'R_UNINITIALIZED' && failed.diagnostic.line === 1 && failed.diagnostic.column > 0,
            'Runtime diagnostic must retain source location');
        const invalid = (await execute(new Uint8Array([0]))).outcome;
        check(invalid.diagnostic.code === 'R_TRAP', 'Incompatible/corrupt program must fail clearly');
        const loop = await compile('program p; begin while true do begin end end.');
        let leaked = '';
        const run = engine.run(loop.binary, {onOutput:text => { leaked += text; }});
        let busy;
        try { engine.run(plain.binary); } catch (error) { busy = error.code; }
        check(busy === 'ENGINE_BUSY', 'Overlapping run must fail explicitly');
        await delay(30);
        const oldWorker = [...workers][0], late = oldWorker.onmessage, oldId = oldWorker.runId;
        check(!(await engine.compile(hello)).diagnostic, 'Compilation may coexist with execution');
        run.stop(); run.stop();
        check((await run.done).status === 'stopped', 'Stop must settle once');
        const next = engine.run(plain.binary);
        late({data:{type:'output', id:oldId, text:'stale'}});
        late({data:{type:'done', id:oldId, files:[{name:'stale'}]}});
        check((await next.done).status === 'completed' && leaked === '', 'Late messages leaked after stop');
        check((await execute(loop.binary)).outcome.diagnostic.code === 'R_TIMEOUT', 'Tight-loop watchdog');
        check((await execute(plain.binary)).outcome.status === 'completed', 'Recovery after timeout');

        /* Callback exceptions never strand the session or expose file snapshots. */
        const callback = engine.run(plain.binary, {onOutput() { throw Error('output callback'); }});
        check((await callback.done).diagnostic.code === 'ENGINE_CALLBACK', 'Output callback failure');
        const terminalStates = [];
        const terminal = engine.run(plain.binary, {onStateChange(state) {
            terminalStates.push(state); if (state === 'completed') throw Error('terminal callback');
        }});
        check((await terminal.done).diagnostic.code === 'ENGINE_CALLBACK' && terminalStates.filter(s => s === 'completed').length === 1,
            'Terminal callback failure must settle without second notification');
        let stopped;
        stopped = engine.run(plain.binary, {onStateChange(state) { if (state === 'running') stopped.stop(); }});
        check((await stopped.done).status === 'stopped', 'Reentrant stop from initial callback');

        /* Seed and generated-file ownership, byte preservation and resource errors
         * use the existing shared fixtures without modifying their batch behavior. */
        for (const fixture of transfers) {
            const compiled = await compile(fixture.source);
            const files = (fixture.files || []).map(f => ({name:f.name, data:new TextEncoder().encode(f.text.repeat(f.repeat ?? 1))}));
            const result = await execute(compiled.binary, {files, input:fixture.input || ''});
            check(result.output === fixture.output, fixture.name + ' raw output');
            if (fixture.runtime) {
                check(result.outcome.status === 'failed' && result.outcome.diagnostic.code === fixture.runtime && !result.outcome.files,
                    fixture.name + ' must publish no files on failure');
            } else {
                const expected = (fixture.resultFiles || []).map(f => ({name:f.name, data:Array.from(new TextEncoder().encode(f.text))}));
                const actual = result.outcome.files.map(f => ({name:f.name, data:Array.from(f.data)}));
                check(JSON.stringify(actual) === JSON.stringify(expected), fixture.name + ' exact files');
            }
        }
        const original = new TextEncoder().encode('original\n');
        const rewrite = await compile(transfers.find(f => f.name === 'rewrite_input').source);
        let originalOutput = '';
        const owned = engine.run(rewrite.binary, {files:[{name:'input.txt',data:original}], onOutput:text => { originalOutput += text; }});
        original.fill(120);
        check((await owned.done).status === 'completed' && originalOutput === 'original\n', 'run must snapshot caller files before returning');
        let shared;
        try { shared = new WebAssembly.Memory({initial:1, maximum:1, shared:true}).buffer; }
        catch (error) { /* Shared memory is optional and never needed by the engine. */ }
        if (shared) {
            const bytes = new Uint8Array(shared, 0, 9); bytes.set(new TextEncoder().encode('original\n'));
            let sharedOutput = '';
            const snapshot = engine.run(rewrite.binary, {files:[{name:'input.txt',data:bytes}], onOutput:text => { sharedOutput += text; }});
            bytes.fill(120);
            check((await snapshot.done).status === 'completed' && sharedOutput === 'original\n', 'Shared byte views must become owned input copies');
        }
        check((await execute(plain.binary, {files:[{name:'bad',data:new Uint8Array([255])}]})).outcome.diagnostic.code === 'R_ENCODING',
            'Host UTF-8 validation');

        /* Real JSPI, conditional on capability, plus deterministic unsupported
         * mode proving batch remains available without replay or transformation. */
        let jspi = false;
        await execute(plain.binary, {onCapabilities(value) { jspi = value.jspi; }});
        metrics.jspi = jspi;
        if (jspi) {
            const mixed = await compile("program p; uses crt; var c:char;s:string; begin c:=readkey; readln(s); writeln(c,'|',s) end.");
            let requests = 0, oldRequest;
            const result = await execute(mixed.binary, {mode:'interactive', onInputRequest(request) {
                requests++;
                if (request.kind === 'key') { oldRequest = request; check(request.submit('AB'), 'Raw key submission'); }
                else {
                    check(request.prefix === 'B' && !oldRequest.submit('late'), 'Prefix and stale bound submission');
                    check(request.submit('BC') && !request.submit('duplicate'), 'Line delivered exactly once');
                }
            }});
            check(result.output === 'A|BC\n' && requests === 2, 'Mixed key/line input');
            const waiting = await compile('program p; var n:integer; begin write(\'Number: \'); readln(n); writeln(n) end.');
            let prompt = '';
            const waited = await execute(waiting.binary, {mode:'interactive', onOutput:text => { prompt += text; },
                onInputRequest(request) {
                    check(prompt === 'Number: ', 'Prompt must flush before input request');
                    setTimeout(() => request.submit('7'), 5200);
                }
            });
            check(waited.output === 'Number: 7\n', 'Human waits must not consume active budget or echo input');
            const eof = await execute(waiting.binary, {mode:'interactive', onInputRequest:request => request.submit(null)});
            check(eof.outcome.diagnostic.code === 'R_INPUT_EOF', 'Interactive EOF');
            const inputCallback = engine.run(waiting.binary, {mode:'interactive', onInputRequest() { throw Error('input callback'); }});
            check((await inputCallback.done).diagnostic.code === 'ENGINE_CALLBACK', 'Input callback failure');
        }
        window.pas2WasmWorkerSource = () => 'WebAssembly.Suspending=undefined;\n' + workerSource();
        check((await execute(plain.binary, {mode:'interactive'})).outcome.diagnostic.code === 'E_JSPI', 'Explicit JSPI capability error');
        check((await execute(plain.binary)).outcome.status === 'completed', 'Batch without JSPI');
        window.pas2WasmWorkerSource = workerSource;

        for (const program of programs) {
            const result = await compile(program.source);
            metrics.programBytes[program.name] = result.binary.byteLength;
        }
        check(requests.length === 3, 'Compiling/running fetched extra assets or sent source/input');

        /* Compare cold-memory calls with an explicitly measured warm worker,
         * without changing production fresh-memory ownership. */
        const [loader, bytes, bootstrap] = await Promise.all([
            nativeFetch(new URL('pas2wasm.js', base)).then(r => r.text()),
            nativeFetch(new URL('pas2wasm.wasm', base)).then(r => r.arrayBuffer()),
            nativeFetch(new URL('compiler-worker.js', base)).then(r => r.text())
        ]);
        const warmUrl = URL.createObjectURL(new Blob([[loader,createPas2WasmCompiler.toString(),bootstrap].join('\n')], {type:'text/javascript'}));
        const warm = new Worker(warmUrl);
        /* One-at-a-time measurement calls use the unchanged low-level protocol. */
        function request(data) { return new Promise((resolve,reject) => {
            warm.onmessage = event => resolve(event.data); warm.onerror = event => reject(Error(event.message)); warm.postMessage(data);
        }); }
        try {
            await request({type:'init',binary:bytes});
            for (let i = 0; i < 5; i++) {
                const start = performance.now(); await request({id:i,source:hello});
                metrics.warmWorkerCompileMs.push(performance.now() - start);
            }
        } finally { warm.terminate(); URL.revokeObjectURL(warmUrl); }

        /* Disposal settles simultaneous run, active compile and queued compile. */
        fault = 'hang';
        const disposalRun = engine.run(loop.binary);
        await delay(30);
        const c = engine.compile(hello), d = engine.compile(hello);
        const cr = rejects(c, 'name', 'AbortError'), dr = rejects(d, 'name', 'AbortError');
        engine.dispose(); engine.dispose(); fault = '';
        await Promise.all([cr, dr]);
        check((await disposalRun.done).status === 'stopped', 'Dispose must settle active run');
        await rejects(engine.compile(hello), 'code', 'ENGINE_DISPOSED');
        let disposedCode;
        try { engine.run(plain.binary); } catch (error) { disposedCode = error.code; }
        check(disposedCode === 'ENGINE_DISPOSED', 'Run after disposal');
        check(workers.size === 0 && urls.size === 0, 'Dispose retained workers/URLs');
        const recovered = await create();
        check(!(await recovered.compile(hello)).diagnostic, 'New engine after disposal');
        recovered.dispose();
        fault = 'startup'; await rejects(Pas2WasmEngine.create({assetBaseUrl:base}), 'code', 'ENGINE_WORKER'); fault = '';
        await rejects(Pas2WasmEngine.create({assetBaseUrl:base + 'missing/'}), 'code', 'ENGINE_LOAD');
        await rejects(Pas2WasmEngine.create({assetBaseUrl:'https://invalid.example/'}), 'code', 'ENGINE_ASSET_URL');
        const loading = new AbortController(); loading.abort();
        await rejects(Pas2WasmEngine.create({assetBaseUrl:base,signal:loading.signal}), 'name', 'AbortError');
        check(workers.size === 0 && urls.size === 0, 'Failed initialization retained resources');
        metrics.retainedWorkers = workers.size; metrics.retainedBlobUrls = urls.size; metrics.peakWorkers = peakWorkers;
        metrics.userAgent = navigator.userAgent;
        return {checks, metrics};
    } finally {
        fault = '';
        for (const engine of engines) engine.dispose();
        for (const worker of workers) worker.terminate();
        for (const url of urls) revokeUrl.call(URL, url);
        window.Worker = NativeWorker; window.fetch = nativeFetch;
        URL.createObjectURL = createUrl; URL.revokeObjectURL = revokeUrl;
        window.pas2WasmWorkerSource = workerSource;
    }
}
