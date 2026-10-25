/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Bind the same keyboard-accessible console to either page. All transcript text
 * is inert; input echo is separate from the program's bounded output callback. */
function createPas2WasmConsole(events, runProgram = null) {
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
    const handlers = {
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
    };
    let currentRun = null;
    /* The playground supplies the public engine; standalone exports retain the
     * same headless controller without embedding or fetching compiler assets. */
    const session = runProgram ? {
        run(binary, input, interactive, files) {
            const run = currentRun = runProgram(binary, {input, files,
                mode:interactive ? 'interactive' : 'batch',
                onOutput:handlers.output,
                onCapabilities(value) { handlers.capabilities(value.jspi); },
                onInputRequest(request) { handlers.waiting(true, request); },
                onStateChange(state) {
                    if (state === 'running') { handlers.waiting(false); handlers.state('Running…'); }
                    else if (state === 'waiting') handlers.state('Waiting for input');
                }
            });
            run.done.then(outcome => {
                if (currentRun !== run) return;
                currentRun = null;
                try {
                    if (outcome.status === 'completed') handlers.files(outcome.files);
                    if (outcome.status === 'failed') handlers.error(outcome.diagnostic);
                    handlers.state({completed:'Completed', failed:'Failed', stopped:'Stopped'}[outcome.status]);
                } catch (error) {
                    handlers.error({code:error.code || 'R_FILE_TYPE', message:error.message}); handlers.state('Failed');
                }
            });
        },
        stop() { const run = currentRun; currentRun = null; run?.stop(); },
        submit(value) {
            const accepted = currentRun?.submit(value) ?? false;
            // EOF discards an editor prefix; it must never become transcript echo.
            if (accepted) handlers.waiting(false, {end:value === null});
            return accepted;
        }
    } : createPas2WasmSession(handlers);
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
