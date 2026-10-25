/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Public browser façade. Make bundles the compiler wrapper, host and headless
 * session before this file; no DOM, loader evaluation or package system is needed.
 * Each engine owns cached assets, a serialized compile queue and at most one run. */
var Pas2WasmEngine = (() => {
    /* Infrastructure failures are separate from Pascal diagnostics;
     * all messages are untrusted display text. */
    function engineError(code, message) {
        return Object.assign(new Error(message), {name:'Pas2WasmEngineError', code:'ENGINE_' + code});
    }

    /* Cancellation consistently rejects promises, independent of signal.reason. */
    function abortError() {
        return new DOMException('The compiler operation was canceled.', 'AbortError');
    }

    /* One bounded compiler worker, including initialization. Assets are borrowed;
     * postMessage clones bytes and transfers only successful output artifacts.
     * Every completion path clears its timer/listener/worker/temporary URL. */
    function compilerRequest(assets, request, signal) {
        return new Promise((resolve, reject) => {
            let worker, url, timer, settled = false, ready = false;
            /* Settle once, invalidating events before disposing their resources. */
            function finish(error, value) {
                if (settled) return;
                settled = true;
                clearTimeout(timer);
                signal.removeEventListener('abort', cancel);
                if (worker) { worker.onmessage = worker.onerror = null; worker.terminate(); }
                if (url) URL.revokeObjectURL(url);
                worker = url = null;
                if (error) reject(error); else resolve(value);
            }
            /* Abort can interrupt initialization and blocking C compilation. */
            function cancel() { finish(abortError()); }
            signal.addEventListener('abort', cancel, {once:true});
            if (signal.aborted) { cancel(); return; }
            try {
                url = URL.createObjectURL(new Blob([assets.script], {type:'text/javascript'}));
                worker = new Worker(url);
                timer = setTimeout(() => finish(engineError('TIMEOUT', 'Compiler operation exceeded 5 seconds.')), 5000);
                worker.onerror = event => finish(engineError('WORKER', event.message || 'Compiler worker failed.'));
                worker.onmessage = ({data}) => {
                    if (settled) return;
                    if (data.type === 'ready' && !ready) {
                        ready = true;
                        if (typeof data.version !== 'string' || !data.version ||
                            (assets.version && assets.version !== data.version)) {
                            finish(engineError('VERSION', 'Compiler assets do not report the expected version.'));
                        } else if (request === null) finish(null, data.version);
                        else {
                            try { worker.postMessage({id:1, ...request}); }
                            catch (error) { finish(engineError('WORKER', error.message)); }
                        }
                    } else if (data.id === 1 && ready && request !== null) {
                        const result = data.result;
                        if (!result || result.diagnostic?.code === 'E_COMPILER')
                            finish(engineError('WORKER', result?.diagnostic?.message || 'Invalid compiler response.'));
                        else finish(null, result);
                    } else if (!ready && data.result?.diagnostic) {
                        finish(engineError('WORKER', data.result.diagnostic.message));
                    }
                };
                worker.postMessage({type:'init', binary:assets.binary});
            } catch (error) { finish(engineError('WORKER', error.message)); }
        });
    }

    /* Load matching same-origin assets once and probe the actual compiler version.
     * The directory is relative to the embedding document, including on subpaths.
     * An optional signal also permits canceling the initial network/worker phase. */
    async function create({assetBaseUrl = './', signal} = {}) {
        const base = new URL(assetBaseUrl, location.href);
        if (base.origin !== location.origin || !['http:', 'https:'].includes(base.protocol))
            throw engineError('ASSET_URL', 'Compiler assets require same-origin HTTP(S) hosting.');
        if (base.search || base.hash)
            throw engineError('ASSET_URL', 'assetBaseUrl must be a directory without a query or fragment.');
        if (!base.pathname.endsWith('/')) base.pathname += '/';
        const loading = new AbortController();
        const cancel = () => loading.abort();
        signal?.addEventListener('abort', cancel, {once:true});
        if (signal?.aborted) loading.abort();
        let timedOut = false;
        const timer = setTimeout(() => { timedOut = true; loading.abort(); }, 5000);
        let assets;
        try {
            const responses = await Promise.all(['pas2wasm.js','pas2wasm.wasm','compiler-worker.js'].map(async path => {
                const response = await fetch(new URL(path, base), {signal:loading.signal, redirect:'error'});
                if (!response.ok) throw engineError('LOAD', `Could not load ${path} (HTTP ${response.status}).`);
                return response;
            }));
            const [loader, binary, bootstrap] = await Promise.all([
                responses[0].text(), responses[1].arrayBuffer(), responses[2].text()
            ]);
            assets = {binary, script:[loader, createPas2WasmCompiler.toString(), bootstrap].join('\n')};
            assets.version = await compilerRequest(assets, null, loading.signal);
        } catch (error) {
            loading.abort();
            if (timedOut) throw engineError('TIMEOUT', 'Compiler loading exceeded 5 seconds.');
            if (signal?.aborted) throw abortError();
            if (error.name === 'Pas2WasmEngineError') throw error;
            throw engineError('LOAD', error.message);
        } finally { clearTimeout(timer); signal?.removeEventListener('abort', cancel); }

        let disposed = false, activeCompile = null, activeRun = null;
        let queue = [];
        const version = assets.version;

        /* Start the next uncanceled request with fresh memory. A terminated worker
         * is never reused; the queue's timeout excludes time waiting for its turn. */
        function pump() {
            if (disposed || activeCompile || !queue.length) return;
            const job = activeCompile = queue.shift();
            compilerRequest(assets, job.request, job.controller.signal).then(
                value => job.finish(null, value), error => job.finish(error)
            ).finally(() => { activeCompile = null; pump(); });
        }

        /* Return a queued promise. Caller source/options are captured at invocation;
         * cancellation/disposal settles immediately and removes retained requests. */
        function compile(source, {emitWat = false, target = 'wasm32', signal} = {}) {
            if (disposed) return Promise.reject(engineError('DISPOSED', 'Engine has been disposed.'));
            if (typeof source !== 'string' || typeof emitWat !== 'boolean' || typeof target !== 'string')
                return Promise.reject(new TypeError('Expected source/target strings and a Boolean emitWat.'));
            if (signal?.aborted) return Promise.reject(abortError());
            if (source.length > 262144 || new TextEncoder().encode(source).length > 262144)
                return Promise.resolve({diagnostic:{code:'E_LIMIT', message:'Source must not exceed 256 KiB.', line:0, column:0}});
            return new Promise((resolve, reject) => {
                // Keep target validation in C, but never pass arbitrary-sized/NUL
                // strings through cwrap's temporary C-string stack allocation.
                const job = {request:{source, emitWat, target:target === 'wasm32' ? target : 'unsupported'},
                    controller:new AbortController(), finish, cancel};
                let settled = false;
                /* Drop request/listener ownership before exposing its result. */
                function finish(error, value) {
                    if (settled) return;
                    settled = true;
                    signal?.removeEventListener('abort', cancel);
                    queue = queue.filter(item => item !== job);
                    job.request = null;
                    if (error) reject(error); else resolve(value);
                }
                /* Abort active work or remove a queued request without starting it. */
                function cancel() { job.controller.abort(); finish(abortError()); }
                signal?.addEventListener('abort', cancel, {once:true});
                queue.push(job); pump();
            });
        }

        /* A public session owns one controller and promise. Only generated Wasm
         * from matching assets is supported. Snapshots preserve caller buffers;
         * callbacks start in a microtask after run has returned the session. */
        function run(binary, options = {}) {
            if (disposed) throw engineError('DISPOSED', 'Engine has been disposed.');
            if (activeRun) throw engineError('BUSY', 'This engine already has an active program.');
            const {mode = 'batch', input = '', files = []} = options;
            if (!['batch','interactive'].includes(mode) || typeof input !== 'string')
                throw new TypeError('Expected batch/interactive mode and string input.');
            for (const key of ['onOutput','onInputRequest','onStateChange','onCapabilities'])
                if (options[key] !== undefined && typeof options[key] !== 'function')
                    throw new TypeError(`${key} must be a function.`);
            if (!(binary instanceof Uint8Array) && !(binary instanceof ArrayBuffer))
                throw new TypeError('Expected compiled Uint8Array or ArrayBuffer bytes.');
            // Clone now, before the deferred worker starts, without detaching inputs.
            const bytes = binary instanceof Uint8Array ? new Uint8Array(binary) : binary.slice(0);
            // Copy byte views explicitly: structuredClone alone preserves shared
            // backing stores, which must not let later caller edits enter a run.
            const seeds = Array.isArray(files) ? files.map(file => {
                if (file?.data instanceof Uint8Array)
                    return {name:file.name, data:new Uint8Array(file.data)};
                return structuredClone(file);
            }) : structuredClone(files);
            let callbacks = Object.fromEntries(['onOutput','onInputRequest','onStateChange','onCapabilities']
                .map(key => [key, options[key]]));
            let settled = false, pending = null, diagnostic, snapshots = [], resolve;
            const done = new Promise(complete => { resolve = complete; });
            const controller = createPas2WasmSession({
                output(text) { notify('onOutput', text); },
                capabilities(jspi) { notify('onCapabilities', {jspi}); },
                error(value) { diagnostic = value; },
                files(value) { snapshots = value; },
                waiting(waiting, request) {
                    if (!waiting || settled) { pending = null; return; }
                    pending = request.requestId;
                    notify('onInputRequest', {requestId:pending, kind:request.kind || 'line',
                        prefix:request.prefix || '', line:request.line, column:request.column,
                        submit:value => session.submit(value, request.requestId)});
                },
                state(state) {
                    if (state === 'Completed') finish('completed');
                    else if (state === 'Failed') finish('failed', diagnostic);
                    else notify('onStateChange', state === 'Waiting for input' ? 'waiting' : 'running');
                }
            });

            /* A throwing callback is an application failure; stop before resolving
             * done. Terminal callback errors change the outcome without a second
             * terminal notification. Promise-returning callbacks are not awaited. */
            function notify(name, value) {
                if (settled) return;
                try { callbacks[name]?.(value); }
                catch (error) { finish('failed', callbackDiagnostic(error)); }
            }
            /* Callback diagnostics cannot impersonate a located Pascal failure. */
            function callbackDiagnostic(error) {
                return {code:'ENGINE_CALLBACK', message:String(error?.message || error), line:0, column:0};
            }
            /* Release resources before terminal callbacks, allowing safe reentry.
             * Only successful completion publishes caller-owned file snapshots. */
            function finish(status, failure) {
                if (settled) return;
                settled = true; pending = null; controller.stop(); activeRun = null;
                let outcome = status === 'completed' ? {status, files:snapshots} :
                    status === 'failed' ? {status, diagnostic:failure} : {status};
                snapshots = [];
                try { callbacks.onStateChange?.(status); }
                catch (error) { outcome = {status:'failed', diagnostic:callbackDiagnostic(error)}; }
                callbacks = null;
                resolve(outcome);
            }
            // The session never reuses a worker or accepts another session's input.
            const session = Object.freeze({done,
                stop() { finish('stopped'); },
                submit(value, requestId = pending) {
                    if (settled || pending === null || requestId !== pending) return false;
                    if (value !== null && typeof value !== 'string')
                        throw new TypeError('Submit a string or null for EOF.');
                    const accepted = controller.submit(value);
                    if (accepted) pending = null;
                    return accepted;
                }
            });
            activeRun = session;
            queueMicrotask(() => {
                if (settled) return;
                try { controller.run(bytes, input, mode === 'interactive', seeds); }
                catch (error) { finish('failed', {code:'R_WORKER', message:error.message, line:0, column:0}); }
            });
            return session;
        }

        /* Cancel everything owned by this engine; completed results remain owned
         * by their callers. Repeated disposal has no additional effects. */
        function dispose() {
            if (disposed) return;
            disposed = true;
            activeRun?.stop();
            for (const job of [...queue]) job.cancel();
            activeCompile?.cancel();
            assets = null;
        }
        return Object.freeze({version, compile, run, dispose});
    }
    return Object.freeze({create});
})();
