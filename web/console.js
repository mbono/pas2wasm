/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Mount a terminal without owning an engine or execution session.
 * Kept self-contained except for the renderer so offline exports embed this factory. */
function createPas2WasmConsoleWidget(container, {autoFocus = true} = {}) {
    if (!container || container.nodeType !== 1 || container.ownerDocument !== document)
        throw new TypeError('Expected a container element in this document.');
    if (typeof autoFocus !== 'boolean') throw new TypeError('autoFocus must be Boolean.');
    const root = document.createElement('div'); root.className = 'p2w-console';
    const style = document.createElement('style');
    style.textContent = `
.p2w-console .p2w-console-terminal{display:block;white-space:pre-wrap;overflow-wrap:anywhere;overflow:auto;
 box-sizing:border-box;min-height:var(--p2w-console-min-height,160px);max-height:var(--p2w-console-max-height,360px);
 padding:15px;margin:0;border:0;border-radius:6px;background:var(--p2w-console-background,#18362f);
 color:var(--p2w-console-color,#f4f2e8);font:var(--p2w-console-font,.9rem/1.65 ui-monospace,monospace);cursor:text}
.p2w-console .p2w-console-terminal:focus-visible{outline:3px solid #d28542;outline-offset:3px}
.p2w-console .p2w-console-input{display:inline;white-space:pre-wrap;font:inherit;color:inherit;
 background:transparent;border:0;padding:0;margin:0;outline:none;caret-color:var(--p2w-console-caret-color,#ffd58a)}
.p2w-console .p2w-console-input[contenteditable="plaintext-only"]:empty{display:inline-block;min-width:1px}
.p2w-console .p2w-console-input[contenteditable="plaintext-only"]:empty::before{content:"\\200b"}
.p2w-console .p2w-console-input[contenteditable="false"]{caret-color:transparent}
`;
    const view = document.createElement('pre'); view.className = 'p2w-console-terminal';
    view.setAttribute('aria-label', 'Program console');
    const history = document.createElement('span'); history.className = 'p2w-console-output';
    history.setAttribute('role', 'log'); history.setAttribute('aria-live', 'polite');
    const editor = document.createElement('span'); editor.className = 'p2w-console-input';
    editor.setAttribute('role', 'textbox'); editor.setAttribute('aria-multiline', 'false');
    editor.spellcheck = false; editor.setAttribute('autocapitalize', 'off');
    view.append(history, editor); root.append(style, view);
    container.appendChild(root);
    let disposed = false, generation = 0, pending = null;
    const terminal = createPas2WasmTerminal(view, history, editor, submit, autoFocus);
    /* A pending request is borrowed from one connection; always use its bound
     * submit rather than whichever request an application session currently has. */
    function submit(value) {
        if (!pending || disposed) return false;
        const request = pending, at = generation;
        const accepted = request.submit(value);
        if (accepted && at === generation && !disposed) {
            pending = null;
            terminal.waiting(false, request.kind);
        }
        return accepted;
    }
    /* Public operations fail consistently after disposal; stale event callbacks
     * instead become inert so removing a widget never crashes an application run. */
    function assertLive() {
        if (disposed) throw new DOMException('Console has been disposed.', 'InvalidStateError');
    }
    /* Each run gets its own callbacks. Replacing/ending connections invalidates
     * late output/input and retains no request-ID collection across long runs. */
    function createHandlers() {
        assertLive();
        const at = ++generation;
        let lastRequest = 0;
        pending = null; terminal.clear();
        return Object.freeze({
            onOutput(text) { if (!disposed && at === generation) terminal.append(text); },
            onInputRequest(request) {
                if (disposed || at !== generation || request.requestId <= lastRequest) return;
                if (!Number.isSafeInteger(request.requestId) || request.requestId < 1 || typeof request.submit !== 'function')
                    throw new TypeError('Expected an engine input request with a bound submit function.');
                lastRequest = request.requestId; pending = request;
                terminal.waiting(true, request.kind || 'line', request.prefix);
            },
            onStateChange(state) {
                if (disposed || at !== generation) return;
                if (state === 'running') {
                    pending = null; terminal.waiting(false);
                } else if (['completed', 'failed', 'stopped'].includes(state)) {
                    generation++; pending = null;
                    editor.replaceChildren(); terminal.waiting(false);
                }
            }
        });
    }
    return Object.freeze({createHandlers,
        clear() { assertLive(); terminal.clearOutput(); },
        focus() { assertLive(); terminal.focus(); },
        dispose() {
            if (disposed) return;
            disposed = true; generation++; pending = null;
            terminal.dispose(); root.remove();
        }
    });
}

/* Classic-script public name, separate from the compiler and headless engine. */
var Pas2WasmConsole = Object.freeze({create:createPas2WasmConsoleWidget});
