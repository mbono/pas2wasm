/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* One immutable transcript and one inline editing root share a scrolling pre.
 * Native browser editing owns Unicode/caret/IME; only submitted text is committed. */
function createPas2WasmTerminal(view, history, editor, submit) {
    if (!document.getElementById('pas2wasm-terminal-style')) {
        const style = document.createElement('style');
        style.id = 'pas2wasm-terminal-style';
        style.textContent = `
#terminal{display:block;white-space:pre-wrap;overflow-wrap:anywhere;overflow:auto;
 min-height:160px;max-height:360px;padding:15px;margin:0;border-radius:6px;
 background:#18362f;color:#f4f2e8;font:.9rem/1.65 ui-monospace,monospace;cursor:text}
#terminal:focus-visible{outline:3px solid #d28542;outline-offset:3px}
#console-input{display:inline;white-space:pre-wrap;font:inherit;color:inherit;
 background:transparent;border:0;padding:0;margin:0;outline:none;caret-color:#ffd58a}
#console-input[contenteditable="plaintext-only"]:empty{display:inline-block;min-width:1px}
#console-input[contenteditable="plaintext-only"]:empty::before{content:"\\200b"}
#console-input[contenteditable="false"]{caret-color:transparent}
#console-controls{margin-top:8px}
`;
        document.head.appendChild(style);
    }
    const committed = document.createTextNode('');
    history.replaceChildren(committed);
    let active = false, composing = false, compositionEnter = false;
    /* A selection is editable only when both endpoints belong to the tail. */
    function inside(selection) {
        return selection && selection.rangeCount && editor.contains(selection.anchorNode) &&
            editor.contains(selection.focusNode);
    }
    /* Focus without moving a valid editing selection or scrolling the whole page. */
    function focus() {
        if (!active) return;
        editor.focus({preventScroll:true});
        const selection = window.getSelection();
        if (!inside(selection)) selection.collapse(editor, editor.childNodes.length);
    }
    /* Return a safe insertion range, never a range spanning transcript history. */
    function range() {
        focus();
        return window.getSelection().getRangeAt(0);
    }
    /* Follow the tail only when the user was already reading the latest text. */
    function append(text, force = false) {
        const follow = view.scrollHeight - view.clientHeight - view.scrollTop <= 4;
        committed.appendData(text);
        if (follow || force) view.scrollTop = view.scrollHeight;
    }
    /* Toggle only the editing root; a disabled tail has no active caret/tab stop. */
    function waiting(value) {
        active = value; composing = compositionEnter = false;
        editor.contentEditable = value ? 'plaintext-only' : 'false';
        editor.setAttribute('aria-disabled', String(!value));
        editor.tabIndex = value ? 0 : -1;
        view.tabIndex = value ? -1 : 0;
        if (value) { focus(); view.scrollTop = view.scrollHeight; }
        else if (document.activeElement === editor) editor.blur();
    }
    /* Freeze once after the session accepts the line; output arrives in later tasks. */
    function enter() {
        if (!active || composing) return;
        const text = editor.textContent;
        if (submit(text)) { append(text + '\n', true); editor.replaceChildren(); }
    }
    /* Detect an editing boundary without splitting Unicode characters inside it. */
    function edge(end) {
        const selection = window.getSelection();
        if (!inside(selection) || !selection.isCollapsed) return false;
        const before = document.createRange();
        before.selectNodeContents(editor);
        before.setEnd(selection.anchorNode, selection.anchorOffset);
        return before.toString().length === (end ? editor.textContent.length : 0);
    }
    /* Native empty editors may retain a BR placeholder; it must not move the
     * next input onto a line that the program did not print. */
    function normalizeEmpty() {
        if (!active || composing || editor.textContent !== '' || !editor.childNodes.length) return;
        editor.replaceChildren();
        if (document.activeElement === editor) window.getSelection().collapse(editor, 0);
    }
    editor.addEventListener('keydown', event => {
        if (!active) return;
        if (composing || event.isComposing || event.keyCode === 229) {
            if (event.key === 'Enter' || event.keyCode === 229) compositionEnter = true;
            return;
        }
        compositionEnter = false;
        if (event.key === 'Enter') { event.preventDefault(); enter(); }
        else if (event.key === 'Home' || event.key === 'End') {
            event.preventDefault(); focus();
            const selection = window.getSelection(), offset = event.key === 'Home' ? 0 : editor.childNodes.length;
            if (event.shiftKey) selection.extend(editor, offset);
            else selection.collapse(editor, offset);
        } else if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'a') {
            event.preventDefault();
            const selected = document.createRange(); selected.selectNodeContents(editor);
            const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(selected);
        } else if (((event.key === 'Backspace' || event.key === 'ArrowLeft') && edge(false)) ||
                   ((event.key === 'Delete' || event.key === 'ArrowRight') && edge(true))) event.preventDefault();
    });
    editor.addEventListener('beforeinput', event => {
        if (!active) { event.preventDefault(); return; }
        if (event.inputType === 'insertParagraph' || event.inputType === 'insertLineBreak') {
            event.preventDefault();
            if (!composing && !event.isComposing && !compositionEnter) enter();
        } else if (!inside(window.getSelection())) {
            event.preventDefault(); focus();
        }
    });
    editor.addEventListener('compositionstart', () => { composing = true; });
    editor.addEventListener('compositionend', () => { composing = false; normalizeEmpty(); });
    /* Some IMEs end composition before delivering the same Enter's beforeinput. */
    editor.addEventListener('keyup', event => {
        if (event.key === 'Enter' || event.keyCode === 229) compositionEnter = false;
    });
    editor.addEventListener('input', normalizeEmpty);
    editor.addEventListener('paste', event => {
        event.preventDefault();
        if (!active) return;
        const text = event.clipboardData.getData('text/plain').replace(/\r\n|[\r\n]/g, ' ');
        const selected = range(), node = document.createTextNode(text);
        selected.deleteContents(); selected.insertNode(node); selected.setStartAfter(node); selected.collapse(true);
        const selection = window.getSelection(); selection.removeAllRanges(); selection.addRange(selected);
        normalizeEmpty();
    });
    editor.addEventListener('cut', event => {
        if (!inside(window.getSelection())) event.preventDefault();
    });
    view.addEventListener('click', event => {
        const selection = window.getSelection();
        if (!editor.contains(event.target) && selection.isCollapsed) focus();
    });
    view.addEventListener('focus', () => { if (active) focus(); });
    waiting(false);
    return {
        append, waiting,
        clear() { committed.data = ''; editor.replaceChildren(); waiting(false); view.scrollTop = 0; }
    };
}
