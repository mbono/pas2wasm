/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Bind the same keyboard-accessible console to either page. All transcript text
 * is inert; input echo is separate from the program's bounded output callback. */
function createPas2WasmConsole(events, runProgram = null) {
    const get = id => document.getElementById(id);
    const mode = get('io-mode'), batch = get('batch-input');
    const note = get('io-note');
    const files = get('program-files') ? createPas2WasmFiles(get('program-files'), value => events.filesReady?.(value)) : null;
    const mount = get('console-mount');
    const widget = Pas2WasmConsole.create(mount);
    // Keep the example pages' existing selectors; the public widget owns no IDs.
    for (const [part, id] of [['terminal','terminal'],['output','output'],['input','console-input'],
                              ['controls','console-controls'],['eof','console-eof']])
        mount.querySelector('.p2w-console-' + part).id = id;
    let connection = widget.createHandlers(), currentRun = null, activeRequest = null;
    const controller = runProgram ? null : createPas2WasmSession({
        output(text) { connection.onOutput(text); },
        error:events.error,
        files(value) { files?.results(value); },
        capabilities,
        waiting(value, request) {
            activeRequest = value ? request.requestId : null;
            if (value) connection.onInputRequest({...request, submit(text) {
                return activeRequest === request.requestId && controller.submit(text);
            }});
        },
        state(text) {
            connection.onStateChange({'Running…':'running', 'Waiting for input':'waiting',
                Completed:'completed', Failed:'failed', Stopped:'stopped'}[text]);
            state(text);
        }
    });
    /* Reflect execution without making widgets own the worker or file panel. */
    function state(text) {
        const active = ['Running…','Waiting for input'].includes(text);
        files?.busy(active);
        if (text === 'Failed') files?.clearOutputs();
        events.state(text);
    }
    /* Capability messaging belongs to the application, outside the terminal. */
    function capabilities(supported) {
        note.textContent = supported ? '' : 'Live console needs a browser with JSPI support. Batch input remains available.';
    }
    /* The playground supplies the engine; offline exports use the same callbacks
     * with their compiler-free execution controller and request-bound submission. */
    const session = {
        run(binary, input, interactive, seedFiles) {
            session.stop(); connection = widget.createHandlers();
            if (!runProgram) { controller.run(binary, input, interactive, seedFiles); return; }
            const callbacks = connection;
            const run = currentRun = runProgram(binary, {input, files:seedFiles,
                mode:interactive ? 'interactive' : 'batch', ...callbacks,
                onCapabilities(value) { capabilities(value.jspi); },
                onStateChange(value) {
                    callbacks.onStateChange(value);
                    if (value === 'running') state('Running…');
                    else if (value === 'waiting') state('Waiting for input');
                }
            });
            run.done.then(outcome => {
                if (currentRun !== run) return;
                currentRun = null;
                try {
                    if (outcome.status === 'completed') files?.results(outcome.files);
                    if (outcome.status === 'failed') events.error(outcome.diagnostic);
                    state({completed:'Completed', failed:'Failed', stopped:'Stopped'}[outcome.status]);
                } catch (error) {
                    events.error({code:error.code || 'R_FILE_TYPE', message:error.message}); state('Failed');
                }
            });
        },
        stop() {
            const run = currentRun; currentRun = null; activeRequest = null;
            run?.stop(); controller?.stop(); connection.onStateChange('stopped');
        }
    };
    /* Switch explicitly between interactive delivery and preloaded input. */
    function updateMode() {
        batch.hidden = mode.value !== 'batch';
        get('console-controls').hidden = get('console-input').hidden = mode.value !== 'interactive';
    }
    mode.addEventListener('change', () => {
        session.stop(); files?.busy(false); files?.clearOutputs(); events.cancel?.(); updateMode();
    });
    updateMode();
    return {
        run(binary) {
            if (!files || files.ready()) {
                files?.clearOutputs();
                session.run(binary, mode.value === 'batch' ? get('input').value : '', mode.value === 'interactive', files?.inputs() ?? []);
            }
        },
        stop() { session.stop(); files?.busy(false); files?.clearOutputs(); },
        clear() { widget.clear(); files?.clearOutputs(); },
        busy(value) { files?.busy(value); },
        ready() { return files?.ready() ?? true; },
        clearFiles() { files?.clearOutputs(); },
        resetFiles() { files?.reset(); },
        interactive() { return mode.value === 'interactive'; }
    };
}
