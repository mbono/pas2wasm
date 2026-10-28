/* Copyright (c) 2026 Mariano Bono; SPDX-License-Identifier: MIT */

/* Exercise the published adapter with native DOM events and independent bindings.
 * Real engines/trusted input and offline behavior are checked by the Python suites. */
async function verifyPas2WasmConsole(base) {
    let checks = 0;
    /* Fail at the violated component contract rather than returning partial success. */
    function check(value, message) { checks++; if (!value) throw Error(message); }
    const outside = document.createElement('input'), mount = document.createElement('div');
    const sentinel = document.createElement('span'); sentinel.textContent = 'Host content';
    mount.append(sentinel); document.body.append(outside, mount);
    const first = Pas2WasmConsole.create(mount, {autoFocus:false});
    const second = Pas2WasmConsole.create(mount);
    const [a,b] = mount.querySelectorAll('.p2w-console');
    const input = a.querySelector('.p2w-console-input'), output = a.querySelector('.p2w-console-output');
    const view = a.querySelector('.p2w-console-terminal');
    const calls = [];
    let engine, ownedWidget;
    try {
        check(mount.firstChild === sentinel && !mount.querySelector('[id]'), 'Owned DOM must preserve host content and need no IDs');
        check(!mount.querySelector('button, .p2w-console-controls'), 'Consoles must not generate execution or EOF controls');
        a.style.setProperty('--p2w-console-background', 'rgb(1, 2, 3)');
        a.style.setProperty('--p2w-console-min-height', '77px');
        a.style.setProperty('--p2w-console-font', '18px/1.5 monospace');
        check(getComputedStyle(view).backgroundColor === 'rgb(1, 2, 3)' && getComputedStyle(view).minHeight === '77px' &&
            getComputedStyle(view).fontSize === '18px', 'CSS variables must style only the selected widget');
        check(getComputedStyle(b.querySelector('pre')).backgroundColor !== 'rgb(1, 2, 3)', 'Styles leaked across instances');
        const old = first.createHandlers(), other = second.createHandlers();
        old.onOutput('ab\bC'); other.onOutput('<img src=x>');
        check(output.textContent === 'aC' && b.querySelector('.p2w-console-output').textContent === '<img src=x>' && !mount.querySelector('img'),
            'Cursor semantics/text-only output and instance isolation');
        old.onOutput('\b'); // Exercise clear/focus while the editor straddles an output suffix.
        outside.focus();
        old.onInputRequest({requestId:1,kind:'line',prefix:'é😀',submit(value) { calls.push(value); return true; }});
        check(document.activeElement === outside && input.contentEditable === 'plaintext-only', 'autoFocus false stole focus');
        first.focus();
        check(document.activeElement === input, 'Explicit focus failed');
        const selection = window.getSelection(); selection.collapse(input.firstChild, 1);
        const node = selection.anchorNode, offset = selection.anchorOffset;
        first.clear();
        check(output.textContent === '' && input.textContent === 'é😀' && document.activeElement === input &&
            selection.anchorNode === node && selection.anchorOffset === offset && input.contentEditable === 'plaintext-only', 'Clear lost draft/request/selection');
        const shortcut = new KeyboardEvent('keydown',{key:'d',ctrlKey:true,bubbles:true,cancelable:true});
        input.dispatchEvent(shortcut);
        check(!shortcut.defaultPrevented && calls.length === 0 && input.textContent === 'é😀', 'Ctrl+D must remain a browser shortcut during a line wait');
        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
        check(calls.length === 1 && calls[0] === 'é😀' && input.textContent === '' && input.contentEditable === 'false', 'Prefix must submit and echo exactly once');
        old.onInputRequest({requestId:1,kind:'line',submit() { throw Error('duplicate'); }});
        check(input.contentEditable === 'false', 'A duplicate input request was accepted');
        old.onInputRequest({requestId:2,kind:'line',submit(value) { calls.push(value); return true; }});
        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
        check(calls.at(-1) === '' && output.textContent === 'é😀\n\n', 'Empty Enter must submit a line rather than EOF');
        old.onInputRequest({requestId:3,kind:'line',submit(value) { calls.push(value); return true; }});
        input.textContent = 'line'; selection.collapse(input.firstChild, 4);
        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
        check(calls.at(-1) === 'line' && output.textContent === 'é😀\n\nline\n' && input.contentEditable === 'false', 'Line must echo exactly once');
        old.onInputRequest({requestId:4,kind:'key',submit(value) { calls.push(value); return true; }});
        const rawShortcut = new KeyboardEvent('keydown',{key:'d',ctrlKey:true,bubbles:true,cancelable:true});
        input.dispatchEvent(rawShortcut);
        check(!rawShortcut.defaultPrevented && calls.length === 3, 'Ctrl+D must remain a browser shortcut during a raw-key wait');
        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Backspace',bubbles:true,cancelable:true}));
        check(calls.at(-1) === '\b' && output.textContent === 'é😀\n\nline\n', 'Raw controls must submit without echo');
        const fresh = first.createHandlers();
        old.onOutput('stale'); old.onInputRequest({requestId:4,submit() { throw Error('stale'); }}); old.onStateChange('failed');
        check(output.textContent === '' && input.contentEditable === 'false', 'Replacing connections must invalidate all old callbacks');
        fresh.onInputRequest({requestId:1,kind:'line',prefix:'new',submit(value) { calls.push(value); return true; }});
        fresh.onStateChange('completed'); fresh.onOutput('late');
        check(input.textContent === '' && output.textContent === '' && input.contentEditable === 'false', 'Terminal state did not invalidate the connection');
        const focusHandlers = second.createHandlers(); outside.focus();
        focusHandlers.onInputRequest({requestId:1,kind:'line',submit() { return true; }});
        check(document.activeElement === b.querySelector('.p2w-console-input'), 'Default automatic focus failed');
        const detachedInput = input, before = calls.length;
        const disposedHandlers = first.createHandlers();
        disposedHandlers.onInputRequest({requestId:1,kind:'line',submit(value) { calls.push(value); return true; }});
        first.dispose(); first.dispose();
        detachedInput.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));
        disposedHandlers.onOutput('late'); disposedHandlers.onInputRequest(null); disposedHandlers.onStateChange('running');
        check(calls.length === before && !a.isConnected && b.isConnected && sentinel.isConnected, 'Disposal leaked callbacks/listeners or removed host/other DOM');
        for (const method of ['createHandlers','clear','focus']) {
            let error; try { first[method](); } catch (value) { error = value; }
            check(error?.name === 'InvalidStateError', 'Disposed ' + method + ' should fail predictably');
        }
        // Repeated mounts own/remove styles rather than accumulating global nodes.
        for (let i=0; i<20; i++) Pas2WasmConsole.create(mount).dispose();
        check(mount.querySelectorAll('style').length === 1, 'Repeated disposal retained styles');
        // Disposing a component must not terminate an application-owned session.
        engine = await Pas2WasmEngine.create({assetBaseUrl:base});
        ownedWidget = Pas2WasmConsole.create(mount, {autoFocus:false});
        const callbacks = ownedWidget.createHandlers();
        const compiled = await engine.compile('program p;var n:integer;begin readln(n);writeln(n)end.');
        let waiting;
        const requested = new Promise(resolve => { waiting = resolve; });
        const session = engine.run(compiled.binary, {mode:'interactive', ...callbacks,
            onInputRequest(request) { callbacks.onInputRequest(request); waiting(); },
            onCapabilities(value) { if (!value.jspi) waiting(); }
        });
        await requested;
        ownedWidget.dispose();
        let busy;
        try { engine.run(compiled.binary); } catch(error) { busy = error.code; }
        // Without JSPI the run may already have failed; retain synchronous batch coverage.
        if (busy) { check(busy === 'ENGINE_BUSY', 'Widget disposal changed engine ownership'); session.stop(); }
        await session.done;
        const recovered = engine.run(compiled.binary, {input:'7'});
        check((await recovered.done).status === 'completed', 'Engine recovery after widget disposal');
        return {checks};
    } finally { engine?.dispose(); ownedWidget?.dispose(); first.dispose(); second.dispose(); outside.remove(); mount.remove(); }
}
