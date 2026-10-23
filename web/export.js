/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Attach a compiled module to the minimal exported runner. All dependencies are
 * embedded; program data is Base64, so source strings cannot break HTML markup. */
function exportedPas2WasmRunner(encoded) {
    const diagnostics = document.getElementById('diagnostics');
    const run = document.getElementById('run'), stop = document.getElementById('stop');
    const binary = Uint8Array.from(atob(encoded), character => character.charCodeAt(0));
    /* Reflect both running and suspended states without losing the live worker. */
    function state(text) {
        const active = ['Running…','Waiting for input'].includes(text);
        document.getElementById('status').textContent = text;
        run.disabled = active; stop.disabled = !active;
    }
    const console = createPas2WasmConsole({
        state,
        error(d) { diagnostics.textContent = `${d.line ? d.line + ':' + d.column + ': ' : ''}${d.code}: ${d.message}`; },
        cancel() { state('Stopped — input mode changed'); }
    });
    run.addEventListener('click', () => { console.clear(); diagnostics.textContent = ''; console.run(binary); });
    stop.addEventListener('click', () => { console.stop(); state('Stopped'); });
}

/* Produce a single HTML document containing only the program and matching host. */
function exportPas2WasmHtml(binary) {
    let raw = '';
    for (const byte of binary) raw += String.fromCharCode(byte);
    const encoded = btoa(raw);
    const script = [createPas2WasmRuntime, pas2WasmProgramWorker, pas2WasmWorkerSource,
        createPas2WasmTerminal, createPas2WasmSession, createPas2WasmConsole,
        exportedPas2WasmRunner].map(fn => fn.toString()).join('\n') +
        `\nexportedPas2WasmRunner(${JSON.stringify(encoded)});`;
    return `<!doctype html>
<!--
MIT License

Copyright (c) 2026 Mariano Bono

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Pas2Wasm program</title><style>
body{font:16px/1.6 system-ui,sans-serif;background:#f5f3eb;color:#213b37;max-width:800px;margin:40px auto;padding:20px}
textarea{box-sizing:border-box;width:100%;padding:12px;font:15px/1.5 monospace}button{padding:10px 18px;margin:16px 8px 16px 0;cursor:pointer}
pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#e9ede2;padding:16px;min-height:48px}#diagnostics{color:#873b26}label{display:block;font-weight:600}
[hidden]{display:none!important}button:focus-visible,select:focus-visible{outline:3px solid #d28542}
</style></head><body><h1>Pas2Wasm program</h1><p>This compiled Pascal program runs locally. Choose batch input before Run, or type into the interactive console when the program asks for input.</p>
<label for="io-mode">Input mode</label><select id="io-mode"><option value="batch">Batch input</option><option value="interactive" selected>Interactive console</option></select><p id="io-note" role="status"></p>
<section id="batch-input"><label for="input">Program input</label><textarea id="input" rows="5" spellcheck="false"></textarea></section>
<button id="run">Run program</button><button id="stop" disabled>Stop</button><span id="status" role="status" aria-live="polite">Ready</span>
<h2>Console</h2><pre id="terminal" tabindex="0" aria-label="Program console"><span id="output" role="log" aria-live="polite"></span><span id="console-input" role="textbox" aria-label="Console input line" aria-multiline="false" contenteditable="false" aria-disabled="true" tabindex="-1" spellcheck="false" autocapitalize="off"></span></pre><div id="console-controls" hidden><button id="console-eof" type="button" disabled>End input</button></div><h2>Diagnostics</h2><pre id="diagnostics" aria-live="polite"></pre>
<p>Limits: 1 MiB input/output, 64 MiB memory, 5 seconds active execution (input waiting excluded). No network connection is needed.</p>
<script>${script}<` + `/script></body></html>`;
}
