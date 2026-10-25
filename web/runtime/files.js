/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Own a bounded in-memory input selection and successful result snapshots. This
 * controller is self-contained for offline exports; it never reads a PC path. */
function createPas2WasmFiles(container, ready = () => {}) {
    const encoder = new TextEncoder();
    const decoder = new TextDecoder('utf-8', {fatal:true, ignoreBOM:true});
    let inputs = [], outputs = [], busy = false, loading = false, generation = 0;
    /* Construct only static elements; filenames and errors always enter textContent. */
    function element(tag, text, id) {
        const node = document.createElement(tag);
        if (text !== undefined) node.textContent = text;
        if (id) node.id = id;
        return node;
    }
    if (!document.getElementById('pas2wasm-files-style')) {
        const style = element('style', `
#program-files{margin-top:24px;padding-top:20px;border-top:1px solid #c7d0c0}
#program-files h3{font-size:.85rem;margin:20px 0 8px}
#program-files p{font-size:.85rem;line-height:1.6;overflow-wrap:anywhere}
#program-files ul{list-style:none;padding:0;margin:8px 0}
#program-files li{display:flex;align-items:center;gap:10px;margin:8px 0}
#program-files li span{flex:1;min-width:0;overflow-wrap:anywhere;white-space:pre-wrap}
#program-files li button{flex:none;margin:0;padding:6px 10px}
#program-files input{max-width:100%;font:inherit;font-size:.85rem}
#program-files button:focus-visible,#program-files input:focus-visible{outline:3px solid #d28542}
#file-status{color:#873b26}
`, 'pas2wasm-files-style');
        document.head.appendChild(style);
    }
    const heading = element('h2', 'Files');
    const label = element('label', 'Add input text files'); label.htmlFor = 'input-files';
    const picker = element('input', undefined, 'input-files'); picker.type = 'file'; picker.multiple = true;
    const budget = element('p', '', 'file-budget');
    const hint = element('p', 'UTF-8 text files. Each Run starts from your selected files. Inputs and generated files share 32 names and 1 MiB of contents.');
    const inputList = element('ul', undefined, 'input-file-list'); inputList.setAttribute('aria-label','Selected input files');
    const clear = element('button', 'Clear inputs', 'clear-files'); clear.type = 'button';
    const message = element('p', '', 'file-status'); message.setAttribute('role','status');
    const outputHeading = element('h3', 'Generated text files');
    const outputStatus = element('p', 'Files from a successful run appear here.', 'output-file-status');
    const outputList = element('ul', undefined, 'output-file-list'); outputList.setAttribute('aria-label','Generated text files');
    container.replaceChildren(heading, label, picker, budget, hint, inputList, clear, message,
        outputHeading, outputStatus, outputList);
    /* Stable runtime diagnostic families also describe invalid host selections. */
    function fail(code, text) { throw Object.assign(new Error(text), {code}); }
    /* Metadata preflight runs before File.arrayBuffer, including every duplicate. */
    function metadata(files, size) {
        if (!Array.isArray(files)) fail('R_FILE_TYPE', 'Expected a list of text files.');
        if (files.length > 32) fail('R_FILE_LIMIT', 'Select at most 32 input files.');
        const names = new Set(); let total = 0;
        for (const file of files) {
            const name = file?.name;
            if (typeof name !== 'string' || !name || name.length > 255 || name.includes('\0'))
                fail('R_FILE_NAME', 'Filenames require 1..255 UTF-8 bytes without NUL.');
            for (const scalar of name) {
                const value = scalar.codePointAt(0);
                if (value >= 0xd800 && value <= 0xdfff)
                    fail('R_FILE_NAME', 'A filename contains invalid Unicode.');
            }
            if (encoder.encode(name).length > 255 || names.has(name))
                fail('R_FILE_NAME', `Filename is too long or duplicated: ${name}`);
            names.add(name);
            const bytes = size(file);
            if (!Number.isInteger(bytes) || bytes < 0)
                fail('R_FILE_TYPE', 'File contents have an invalid byte size.');
            total += bytes;
            if (total > 1048576) fail('R_FILE_LIMIT', 'Selected files exceed 1 MiB.');
        }
        return total;
    }
    /* Preserve exact bytes: decoding checks UTF-8 but never re-encodes the file. */
    function validate(files) {
        metadata(files, file => {
            if (!(file.data instanceof Uint8Array)) fail('R_FILE_TYPE', 'Expected file bytes.');
            return file.data.byteLength;
        });
        for (const file of files) {
            try { decoder.decode(file.data); }
            catch { fail('R_ENCODING', `Not valid UTF-8: ${file.name}`); }
        }
    }
    /* Only the suggested PC filename is sanitized; Pascal's exact name is retained. */
    function diskName(name) {
        const safe = name.replace(/[\\/:*?"<>|\u0000-\u001f\u007f]/g, '_').replace(/^[ .]+|[ .]+$/g, '');
        return safe || 'program-file.txt';
    }
    /* A click owns one temporary URL, released after the browser starts saving it. */
    function download(file) {
        const url = URL.createObjectURL(new Blob([file.data], {type:'text/plain;charset=utf-8'}));
        const link = element('a'); link.href = url; link.download = diskName(file.name); link.click();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    }
    /* Render bounded rows, keeping actions unavailable during a run or selection. */
    function render() {
        const disabled = busy || loading;
        picker.disabled = disabled; clear.disabled = disabled || !inputs.length;
        const total = inputs.reduce((sum, file) => sum + file.data.byteLength, 0);
        budget.textContent = `${inputs.length}/32 input files · ${total}/1048576 bytes selected · ${1048576-total} bytes available at Run start`;
        inputList.replaceChildren(); outputList.replaceChildren();
        for (const [index, file] of inputs.entries()) {
            const row = element('li'), name = element('span', `${file.name} (${file.data.byteLength} bytes)`);
            const remove = element('button', 'Remove'); remove.type = 'button'; remove.disabled = disabled;
            remove.setAttribute('aria-label', `Remove ${file.name}`);
            remove.addEventListener('click', () => {
                if (busy || loading) return;
                inputs.splice(index, 1); clearOutputs(); message.textContent = '';
            });
            row.append(name, remove); inputList.appendChild(row);
        }
        for (const file of outputs) {
            const row = element('li'), name = element('span', `${file.name} (${file.data.byteLength} bytes)`);
            const save = element('button', 'Download'); save.type = 'button'; save.disabled = disabled;
            save.setAttribute('aria-label', `Download ${file.name}`);
            save.addEventListener('click', () => { if (!busy && !loading) download(file); });
            row.append(name, save); outputList.appendChild(row);
        }
    }
    /* Results always belong to one successful run, never to another input selection. */
    function clearOutputs() {
        outputs = []; outputStatus.textContent = 'Files from a successful run appear here.';
        render();
    }
    /* Reject an entire batch on error. A Reset invalidates already pending reads. */
    picker.addEventListener('change', async () => {
        if (busy || loading) return;
        const files = Array.from(picker.files), id = ++generation;
        picker.value = ''; message.textContent = '';
        if (!files.length) return;
        try {
            metadata([...inputs, ...files], file => file.data ? file.data.byteLength : file.size);
            loading = true; ready(false); message.textContent = 'Reading input files…'; render();
            const added = [];
            for (const file of files) {
                const data = new Uint8Array(await file.arrayBuffer());
                if (id !== generation) return;
                if (data.byteLength !== file.size) fail('R_FILE_TYPE', 'File size changed while reading.');
                added.push({name:file.name, data});
            }
            validate([...inputs, ...added]);
            inputs.push(...added); clearOutputs(); message.textContent = '';
        } catch (error) {
            if (id === generation) {
                const code = typeof error?.code === 'string' ? error.code : 'R_FILE_STATE';
                message.textContent = `${code}: ${error?.message || 'The selected file could not be read.'}`;
            }
        } finally {
            if (id === generation) { loading = false; render(); ready(true); }
        }
    });
    clear.addEventListener('click', () => {
        if (busy || loading) return;
        inputs = []; message.textContent = ''; clearOutputs();
    });
    render();
    return {
        /* postMessage clones these private buffers; a run cannot alter selections. */
        inputs() { return inputs; },
        ready() { return !loading; },
        busy(value) { busy = value; render(); },
        clearOutputs,
        /* Copy results before retaining them, validating this worker boundary too. */
        results(files) {
            validate(files);
            outputs = files.map(file => ({name:file.name, data:new Uint8Array(file.data)}));
            outputStatus.textContent = outputs.length ? 'Files generated by the last successful run:' : 'No text files were generated.';
            render();
        },
        reset() {
            generation++; loading = false; inputs = []; message.textContent = ''; picker.value = '';
            clearOutputs(); ready(true);
        }
    };
}
if (typeof module !== 'undefined' && module.exports) module.exports = createPas2WasmFiles;
