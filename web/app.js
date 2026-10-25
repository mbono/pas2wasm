/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* The example UI consumes the public engine. Artifacts are invalidated on edits;
 * an abort signal and operation identity discard canceled compilation results. */
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
let engine, compilation;
let operation = 0, busy = false, filesReady = true, binary = null, wat = null;
ui.source.value = exampleSource;
ui.input.value = '21';
const programConsole = createPas2WasmConsole({
    state(text) { status(text, ['Running…','Waiting for input'].includes(text)); },
    error:showDiagnostic,
    filesReady(value) { filesReady = value; ui.run.disabled = busy || !engine || !value; },
    cancel() { terminate(); status('Stopped — input mode changed'); }
}, (bytes, options) => engine.run(bytes, options));

/* Reflect busy state in keyboard-accessible controls. */
function status(text, active = false) {
    busy = active;
    ui.status.textContent = text;
    ui.run.disabled = active || !engine || !filesReady;
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
    compilation?.abort(); compilation = null;
    programConsole.stop();
}

/* Compile off-thread through the public engine, then start a fresh execution. */
async function run() {
    if (!programConsole.ready() || !engine) return;
    terminate(); invalidate();
    programConsole.clear(); ui.diagnostics.textContent = '';
    const sourceBytes = new TextEncoder().encode(ui.source.value).length;
    const inputBytes = programConsole.interactive() ? 0 : new TextEncoder().encode(ui.input.value).length;
    if (sourceBytes > 262144 || inputBytes > 1048576) {
        showDiagnostic({code: 'E_LIMIT', message: sourceBytes > 262144 ? 'Source exceeds 256 KiB.' : 'Input exceeds 1 MiB.'});
        status('Failed'); return;
    }
    const id = operation;
    compilation = new AbortController();
    status('Compiling…', true);
    try {
        const result = await engine.compile(ui.source.value, {
            emitWat:ui['generate-wat'].checked, signal:compilation.signal
        });
        if (id !== operation) return;
        compilation = null;
        if (result.diagnostic) { invalidate(); showDiagnostic(result.diagnostic); status('Failed'); return; }
        binary = result.binary; wat = result.wat;
        ui['wat-view'].textContent = wat || '';
        ui['download-wasm'].disabled = ui['download-html'].disabled = false;
        ui['download-wat'].disabled = !wat;
        programConsole.run(binary);
    } catch (error) {
        if (id !== operation) return;
        invalidate(); showDiagnostic({code:error.code || 'ENGINE_WORKER', message:error.message}); status('Failed');
    }
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

/* Initialization reports the version embedded in the loaded compiler. */
async function loadCompiler() {
    try {
        engine = await Pas2WasmEngine.create({assetBaseUrl:'./'});
        ui['compiler-version'].textContent = ` (v${engine.version})`;
        document.title = `Pas2Wasm (v${engine.version}) — Pascal in your browser`;
        status('Ready');
    } catch (error) { showDiagnostic({code:error.code || 'ENGINE_LOAD', message:error.message}); status('Failed to load'); }
}
// A BFCache restore may reuse the page; only dispose when it is being discarded.
window.addEventListener('pagehide', event => { if (!event.persisted) { terminate(); engine?.dispose(); } });
loadCompiler();
