/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Own one terminable program worker and its cumulative active-time budget.
 * The returned controller is shared by the playground and standalone runner. */
function createPas2WasmSession(events, budget = 5000) {
    let worker = null, url = null, timer, generation = 0, request = null;
    let remaining = budget, started = null, inputBytes = 0;
    /* Release resources and invalidate every queued event from this run. */
    function stop() {
        generation++; clearTimeout(timer); request = null; started = null;
        if (worker) worker.terminate();
        if (url) URL.revokeObjectURL(url);
        worker = url = null;
    }
    /* Complete or fail exactly once, rendering structured errors in the owner. */
    function finish(state, diagnostic) {
        stop();
        if (diagnostic) events.error(diagnostic);
        events.state(state);
    }
    /* Deduct active time when waiting; starting/resuming never replenishes it. */
    function pause() {
        if (started !== null) remaining -= performance.now() - started;
        started = null; clearTimeout(timer);
    }
    /* The page's watchdog also terminates tight loops that cannot send messages. */
    function resume() {
        if (started !== null) return;
        started = performance.now();
        const id = generation;
        timer = setTimeout(() => {
            if (id === generation) finish('Failed', {code:'R_TIMEOUT', message:'Execution exceeded 5 seconds of active time.'});
        }, Math.max(0, remaining));
    }
    /* Start a fresh program with either closed batch input or a live provider. */
    function run(binary, input = '', interactive = false, files = []) {
        stop(); remaining = budget; inputBytes = 0;
        const id = generation;
        url = URL.createObjectURL(new Blob([pas2WasmWorkerSource()], {type:'text/javascript'}));
        worker = new Worker(url);
        events.state('Running…'); resume();
        worker.onmessage = ({data}) => {
            if (id !== generation || data.id !== id) return;
            if (data.type === 'output') events.output(data.text);
            else if (data.type === 'capabilities') events.capabilities?.(data.jspi);
            else if (data.type === 'running') {
                request = null; events.waiting?.(false); resume(); events.state('Running…');
            } else if (data.type === 'input-request') {
                pause();
                if (remaining <= 0) {
                    finish('Failed', {code:'R_TIMEOUT', message:'Execution exceeded 5 seconds of active time.'});
                    return;
                }
                request = data; events.state('Waiting for input'); events.waiting?.(true,data);
            } else if (data.type === 'done') {
                try { events.files?.(data.files === undefined ? [] : data.files); finish('Completed'); }
                catch (error) { finish('Failed', {code:error.code || 'R_FILE_TYPE', message:error.message}); }
            }
            else if (data.type === 'error') finish('Failed', data.diagnostic);
        };
        worker.onerror = event => {
            if (id === generation) finish('Failed', {code:'R_WORKER', message:event.message});
        };
        worker.postMessage({type:'run', id, binary, input, interactive, files});
    }
    /* Key chunks are accepted once without echo/delimiters. A later line may
     * contain an already charged raw suffix; credit those bytes, never its new LF. */
    function submit(text = null) {
        if (!request || !worker) return false;
        const at = request, key = at.kind === 'key';
        if (text !== null) {
            if (typeof text !== 'string' || (key ? !text.length : /[\r\n]/.test(text))) {
                finish('Failed', {code:'R_INPUT_INVALID',message:key ? 'Submit a nonempty key chunk.' : 'Submit one input line.',
                    line:at.line,column:at.column}); return false;
            }
            if (key) text = text.replace(/\r\n|\n/g,'\r');
            const encoder = new TextEncoder();
            const credit = !key && typeof at.prefix === 'string' ? encoder.encode(at.prefix).length : 0;
            inputBytes += Math.max(0,encoder.encode(text).length-credit) + (key ? 0 : 1);
            if (inputBytes > 1048576) {
                finish('Failed', {code:'R_INPUT_LIMIT',message:'Program input exceeds 1 MiB.',
                    line:at.line,column:at.column}); return false;
            }
        }
        worker.postMessage({type:text === null ? 'end-input' : 'input', id:generation,
            requestId:at.requestId, text});
        request = null;
        events.waiting?.(false,{...at,end:text === null});
        // Charge delivery/resumption time too, so a lost worker cannot wait forever.
        resume(); events.state('Running…');
        return true;
    }
    return {run, stop, submit};
}

/* Bind the same keyboard-accessible console to either page. All transcript text
 * is inert; input echo is separate from the program's bounded output callback. */
function createPas2WasmConsole(events) {
    const get = id => document.getElementById(id);
    const mode = get('io-mode'), batch = get('batch-input'), output = get('output');
    const input = get('console-input'), controls = get('console-controls');
    const eof = get('console-eof'), note = get('io-note');
    const files = get('program-files') ? createPas2WasmFiles(get('program-files'), value => events.filesReady?.(value)) : null;
    const terminal = createPas2WasmTerminal(get('terminal'), output, input, text => session.submit(text));
    /* Reflect a pending input request and focus its single-line editor. */
    function waiting(value, request = {}) {
        terminal.waiting(value,request.kind || 'line',request.prefix,request.end); eof.disabled = !value;
    }
    const session = createPas2WasmSession({
        state(text) {
            const active = ['Running…','Waiting for input'].includes(text);
            if (!active) waiting(false);
            files?.busy(active);
            if (text === 'Failed') files?.clearOutputs();
            events.state(text);
        },
        error:events.error,
        files(value) { files?.results(value); },
        output(text) { terminal.append(text); },
        waiting,
        capabilities(supported) {
            note.textContent = supported ? '' : 'Live console needs a browser with JSPI support. Batch input remains available.';
        }
    });
    /* Switch explicitly between interactive delivery and preloaded input. */
    function updateMode() {
        batch.hidden = mode.value !== 'batch';
        controls.hidden = input.hidden = mode.value !== 'interactive';
        waiting(false);
    }
    mode.addEventListener('change', () => {
        session.stop(); files?.busy(false); files?.clearOutputs(); events.cancel?.(); updateMode();
    });
    eof.addEventListener('click', () => session.submit(null));
    updateMode();
    return {
        run(binary) {
            if (!files || files.ready()) {
                files?.clearOutputs();
                session.run(binary, mode.value === 'batch' ? get('input').value : '', mode.value === 'interactive', files?.inputs() ?? []);
            }
        },
        stop() { session.stop(); waiting(false); files?.busy(false); files?.clearOutputs(); },
        clear() { terminal.clear(); waiting(false); files?.clearOutputs(); },
        busy(value) { files?.busy(value); },
        ready() { return files?.ready() ?? true; },
        clearFiles() { files?.clearOutputs(); },
        resetFiles() { files?.reset(); },
        interactive() { return mode.value === 'interactive'; }
    };
}

if (typeof module !== 'undefined' && module.exports)
    module.exports = {createPas2WasmSession};
