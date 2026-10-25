/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Page state owns cached compiler assets, artifacts, worker handles and timers.
 * Artifacts are invalidated on source changes; operation IDs reject stale replies. */
const ui = Object.fromEntries(['source','input','run','stop','reset','status','output','diagnostics',
    'generate-wat','wat-view','download-wasm','download-wat','download-html','compiler-version'].map(id => [id, document.getElementById(id)]));
const exampleSource = `program double;
var
  n: integer;
begin
  readln(n);
  writeln('Twice your number: ', n * 2)
end.
`;
let compilerAssets, compilerWorker, timer;
let operation = 0, busy = false, filesReady = true, binary = null, wat = null;
ui.source.value = exampleSource;
ui.input.value = '21';
const programConsole = createPas2WasmConsole({
    state(text) { status(text, ['Running…','Waiting for input'].includes(text)); },
    error:showDiagnostic,
    filesReady(value) { filesReady = value; ui.run.disabled = busy || !compilerAssets || !value; },
    cancel() { terminate(); status('Stopped — input mode changed'); }
});

/* Reflect busy state in keyboard-accessible controls. */
function status(text, active = false) {
    busy = active;
    ui.status.textContent = text;
    ui.run.disabled = active || !compilerAssets || !filesReady;
    ui.stop.disabled = !active;
    programConsole.busy(active);
}

/* Format stable diagnostics using source byte locations. */
function showDiagnostic(diagnostic) {
    const location = diagnostic.line ? `${diagnostic.line}:${diagnostic.column || 1}: ` : '';
    ui.diagnostics.textContent = `${location}${diagnostic.code}: ${diagnostic.message}`;
}

/* Invalidate artifacts immediately so edits cannot export a previous program. */
function invalidate() {
    binary = null; wat = null;
    programConsole.clearFiles();
    ui['wat-view'].textContent = '';
    for (const type of ['wasm','wat','html']) ui[`download-${type}`].disabled = true;
}

/* Terminate active work and release all associated resources. */
function terminate() {
    ++operation;
    clearTimeout(timer);
    if (compilerWorker) { compilerWorker.terminate(); compilerWorker = null; }
    programConsole.stop();
}

/* Execute a fresh generated instance in an embedded, terminable worker. */
function execute(bytes) {
    programConsole.run(bytes);
}

/* Compile in a fresh worker using cached assets, then execute only on success. */
function run() {
    if (!programConsole.ready()) return;
    terminate(); invalidate();
    programConsole.clear(); ui.diagnostics.textContent = '';
    const sourceBytes = new TextEncoder().encode(ui.source.value).length;
    const inputBytes = programConsole.interactive() ? 0 : new TextEncoder().encode(ui.input.value).length;
    if (sourceBytes > 262144 || inputBytes > 1048576) {
        showDiagnostic({code: 'E_LIMIT', message: sourceBytes > 262144 ? 'Source exceeds 256 KiB.' : 'Input exceeds 1 MiB.'});
        status('Failed'); return;
    }
    const id = operation;
    status('Compiling…', true);
    const url = URL.createObjectURL(new Blob([compilerAssets.script], {type: 'text/javascript'}));
    compilerWorker = new Worker(url);
    URL.revokeObjectURL(url);
    timer = setTimeout(() => {
        if (id !== operation) return;
        terminate(); invalidate(); showDiagnostic({code: 'E_TIMEOUT', message: 'Compilation exceeded 5 seconds.'}); status('Failed');
    }, 5000);
    compilerWorker.onerror = event => {
        if (id !== operation) return;
        terminate(); invalidate(); showDiagnostic({code: 'E_WORKER', message: event.message}); status('Failed');
    };
    /* Initialization is separate from compilation, but both share the time limit. */
    compilerWorker.onmessage = ({data}) => {
        if (id !== operation) return;
        if (data.type === 'ready') {
            compilerWorker.postMessage({id, source: ui.source.value, emitWat: ui['generate-wat'].checked});
            return;
        }
        clearTimeout(timer);
        compilerWorker.terminate(); compilerWorker = null;
        const result = data.result;
        if (result.diagnostic) { invalidate(); showDiagnostic(result.diagnostic); status('Failed'); return; }
        binary = result.binary; wat = result.wat;
        ui['wat-view'].textContent = wat || '';
        ui['download-wasm'].disabled = ui['download-html'].disabled = false;
        ui['download-wat'].disabled = !wat;
        execute(binary);
    };
    compilerWorker.postMessage({type: 'init', binary: compilerAssets.binary});
}

/* Download a bounded, caller-owned artifact; release its temporary object URL. */
function download(name, contents, type) {
    const url = URL.createObjectURL(new Blob([contents], {type}));
    const link = document.createElement('a');
    link.href = url; link.download = name; link.click();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
}
ui.run.addEventListener('click', run);
ui.stop.addEventListener('click', () => { terminate(); status('Stopped'); });
ui.source.addEventListener('input', () => {
    invalidate(); if (busy) { terminate(); status('Stopped — source changed'); }
});
ui['generate-wat'].addEventListener('change', () => {
    wat = null; ui['wat-view'].textContent = ''; ui['download-wat'].disabled = true;
    if (busy) { terminate(); status('Stopped — options changed'); }
});
ui.reset.addEventListener('click', () => {
    terminate(); invalidate(); ui.source.value = exampleSource; ui.input.value = '21';
    ui['generate-wat'].checked = false; programConsole.clear(); programConsole.resetFiles(); ui.diagnostics.textContent = ''; status('Ready');
});
ui['download-wasm'].addEventListener('click', () => { if (binary) download('program.wasm', binary, 'application/wasm'); });
ui['download-wat'].addEventListener('click', () => { if (wat) download('program.wat', wat, 'text/plain;charset=utf-8'); });
ui['download-html'].addEventListener('click', () => {
    if (binary) download('program.html', exportPas2WasmHtml(binary), 'text/html;charset=utf-8');
});

/* Fetch only same-deployment assets once; later workers use these in-memory copies. */
async function loadCompiler() {
    let probe, startupTimer;
    try {
        const paths = ['pas2wasm.js', 'pas2wasm.wasm', 'compiler-worker.js'];
        const responses = await Promise.all(paths.map(path => fetch(path)));
        if (responses.some(response => !response.ok)) throw new Error('A compiler asset could not be loaded.');
        const [loader, bytes, worker] = await Promise.all([
            responses[0].text(), responses[1].arrayBuffer(), responses[2].text()
        ]);
        const assets = {script: loader + '\n' + createPas2WasmCompiler.toString() + '\n' + worker, binary: bytes};
        // Initialize the actual compiler before showing its embedded version.
        const url = URL.createObjectURL(new Blob([assets.script], {type: 'text/javascript'}));
        try { probe = new Worker(url); } finally { URL.revokeObjectURL(url); }
        const version = await new Promise((resolve, reject) => {
            startupTimer = setTimeout(() => reject(new Error('Compiler initialization exceeded 5 seconds.')), 5000);
            probe.onmessage = ({data}) => {
                if (data.type === 'ready') resolve(data.version);
                else reject(new Error(data.result?.diagnostic?.message || 'Compiler initialization failed.'));
            };
            probe.onerror = event => reject(new Error(event.message));
            probe.postMessage({type: 'init', binary: assets.binary});
        });
        if (typeof version !== 'string' || !version) throw new Error('Compiler did not report a version.');
        ui['compiler-version'].textContent = ` (v${version})`;
        document.title = `Pas2Wasm (v${version}) — Pascal in your browser`;
        compilerAssets = assets;
        status('Ready');
    } catch (error) { showDiagnostic({code: 'E_LOAD', message: error.message}); status('Failed to load'); }
    finally { clearTimeout(startupTimer); if (probe) probe.terminate(); }
}
loadCompiler();
