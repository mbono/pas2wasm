# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Native terminal editing, geometry, and renderer-state checks in real browsers."""
import base64
from pathlib import Path
import interactive


def source():
    """Prompt without LF, then show output and wait again on the same surface."""
    return "program demo; var n:integer; begin write('Number: '); readln(n); writeln('Twice: ',n*2); write('Again: '); readln(n); writeln(n) end."


def keys(browser, text, element_id='console-input'):
    """Send trusted WebDriver keystrokes to the inline contenteditable root."""
    element = browser.command('POST','/element',{'using':'css selector','value':'#'+element_id})
    reference = element['element-6066-11e4-a52e-4f735466cecf']
    browser.command('POST',f'/element/{reference}/value',{'text':text})


def verify(browser):
    """Verify native caret layout, editing, composition, paste, Enter, and history."""
    if browser.js("return document.getElementById('io-mode').value;") != 'interactive':
        interactive.select_mode(browser,'interactive')
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    assert browser.js("return document.getElementById('output').textContent;") == 'Number: '
    assert browser.js("return !document.getElementById('console-form') && !document.getElementById('console-submit');")
    assert browser.js("const input=document.getElementById('console-input'),box=input.getBoundingClientRect();return box.width>=1 && box.height>0 && getComputedStyle(input).caretColor==='rgb(255, 213, 138)';")
    keys(browser,'\ue008\ue004\ue000')
    previous = browser.js("return document.activeElement.id;")
    assert previous and previous not in ('console-input','terminal'),previous
    keys(browser,'\ue004',previous)
    assert browser.js("return document.activeElement.id;") == 'console-input'
    keys(browser,'21')
    result = browser.js('''const input=document.getElementById('console-input'),output=document.getElementById('output');
        const prompt=document.createRange();prompt.selectNodeContents(output);prompt.setStart(output.firstChild,output.textContent.length-1);
        const first=document.createRange();first.setStart(input.firstChild,0);first.setEnd(input.firstChild,1);
        const a=prompt.getBoundingClientRect(),b=first.getBoundingClientRect();
        const caret=window.getSelection().getRangeAt(0).getBoundingClientRect();
        return {transcript:output.textContent,draft:input.textContent,visible:document.getElementById('terminal').textContent,
            sameLine:Math.abs(a.top-b.top)<2,adjacent:Math.abs(a.right-b.left)<2,
            caret:caret.height>0&&caret.left>=b.left,focused:document.activeElement===input,
            border:getComputedStyle(input).borderWidth,editable:input.contentEditable};''')
    assert result == dict(transcript='Number: ',draft='21',visible='Number: 21',sameLine=True,
                          adjacent=True,caret=True,focused=True,border='0px',editable='plaintext-only'),result
    suffix = 'offline' if browser.js('return location.protocol;') == 'file:' else 'playground'
    Path(f'build/terminal-{browser.kind}-{suffix}.png').write_bytes(base64.b64decode(browser.command('GET','/screenshot')))
    # A composition Enter must never deliver a line to the waiting program.
    browser.js('''const input=document.getElementById('console-input');
        input.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));
        input.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',isComposing:true,bubbles:true}));
        input.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertParagraph',isComposing:true,bubbles:true,cancelable:true}));''')
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    browser.js("document.getElementById('console-input').dispatchEvent(new CompositionEvent('compositionend',{bubbles:true}));")
    browser.js("const input=document.getElementById('console-input');input.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertParagraph',bubbles:true,cancelable:true}));")
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    browser.js("document.getElementById('console-input').dispatchEvent(new KeyboardEvent('keyup',{key:'Enter',bubbles:true}));")
    # Home/End and deletion at the editing boundaries cannot alter the prompt.
    keys(browser,'\ue011\ue003\ue012\ue017\ue010\ue017\ue014')
    assert browser.js("return document.getElementById('console-input').textContent;") == '1'
    assert browser.js("return document.getElementById('output').textContent;") == 'Number: '
    keys(browser,'\ue009a\ue000')
    assert browser.js("return window.getSelection().toString();") == '1'
    # Paste is always plain text, including markup and Unicode, and never submits.
    browser.page_async('''const input=document.getElementById('console-input'),data=new DataTransfer();
        data.setData('text/plain','é😀<img>\\ntext');data.setData('text/html','<img src=x>');
        // Firefox protects DataTransfer passed to synthetic ClipboardEvent.
        // Supply clipboard data to the actual DOM paste listener instead.
        const event=new Event('paste',{bubbles:true,cancelable:true});
        Object.defineProperty(event,'clipboardData',{value:data});input.dispatchEvent(event);
        arguments[arguments.length-1](true);''')
    pasted = browser.js("return document.getElementById('console-input').textContent;")
    assert pasted == 'é😀<img> text',repr(pasted)
    assert browser.js("return document.querySelectorAll('img').length;") == 0
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    keys(browser,'\ue011\ue014\ue017')
    assert browser.js("return document.getElementById('console-input').textContent;") == 'é<img> text'
    keys(browser,'\ue009a\ue000')
    before=browser.js("const s=window.getSelection(),e=document.getElementById('console-input');return {text:s.toString(),anchor:s.anchorNode.nodeName,focus:s.focusNode.nodeName,inside:e.contains(s.anchorNode)&&e.contains(s.focusNode)};")
    assert before['text']=='é<img> text' and before['inside'],before
    keys(browser,'\ue003')
    assert browser.js("return document.getElementById('console-input').innerHTML;") == ''
    keys(browser,'21')
    after=browser.js("return document.getElementById('console-input').textContent;")
    assert after=='21',(before,repr(after))
    keys(browser,'\ue007')
    browser.wait("document.getElementById('output').textContent==='Number: 21\\nTwice: 42\\nAgain: '")
    assert browser.js("return document.getElementById('console-input').textContent;") == ''
    keys(browser,'8\ue007')
    browser.wait("document.getElementById('status').textContent==='Completed'")
    assert browser.js("return document.getElementById('output').textContent;") == 'Number: 21\nTwice: 42\nAgain: 8\n8\n'
    assert browser.js("return document.getElementById('console-input').contentEditable;") == 'false'
    assert browser.js("return document.activeElement.id;") != 'console-input'
    verify_renderer(browser)
    interactive.select_mode(browser,'batch')


def verify_renderer(browser):
    """Exercise late output and history selection independently of Wasm timing."""
    result = browser.page_async('''const done=arguments[arguments.length-1];
        const view=document.createElement('pre'),history=document.createElement('span'),editor=document.createElement('span');
        view.style.cssText='width:200px;height:80px;max-height:80px;overflow:auto;white-space:pre-wrap;font:16px monospace';
        view.append(history,editor);document.body.append(view);
        try {
            const renderer=createPas2WasmTerminal(view,history,editor,()=>true);
            renderer.append('Prompt: ');renderer.waiting(true);editor.textContent='ab😀cd';
            const selection=window.getSelection(),selected=document.createRange();
            selected.setStart(editor.firstChild,1);selected.setEnd(editor.firstChild,5);
            selection.removeAllRanges();selection.addRange(selected);
            const node=editor.firstChild;renderer.append('notice\\n');
            const preserved=editor.textContent==='ab😀cd' && document.activeElement===editor &&
                selection.anchorNode===node&&selection.anchorOffset===1&&selection.focusOffset===5;
            renderer.append('line\\n'.repeat(100));view.scrollTop=0;
            const copied=document.createRange();copied.setStart(history.firstChild,0);copied.setEnd(history.firstChild,7);
            selection.removeAllRanges();selection.addRange(copied);renderer.append('later\\n');
            view.dispatchEvent(new MouseEvent('click',{bubbles:true}));
            const historyCopy=selection.toString()==='Prompt:' && view.scrollTop===0;
            renderer.waiting(false);renderer.waiting(true);
            const restored=view.scrollTop>0 && document.activeElement===editor;
            renderer.clear();
            done({preserved,historyCopy,restored,cleared:view.textContent==='',disabled:editor.contentEditable==='false'});
        }catch(error){done({error:error.message});}finally{view.remove();}''')
    assert result == dict(preserved=True,historyCopy=True,restored=True,cleared=True,disabled=True),result
    verify_output(browser)


def verify_ui(browser):
    """Compile the terminal fixture through ordinary playground controls."""
    browser.js("const source=document.getElementById('source');source.value=arguments[0];source.dispatchEvent(new Event('input'));",source())
    verify(browser)


def verify_cursor(browser, prefix, suffix):
    """Check the real inline caret before an unoverwritten output character."""
    result=browser.js('''const e=document.getElementById('console-input'),h=document.getElementById('output');
        const s=window.getSelection(),left=h.firstChild,right=h.lastChild;
        const a=document.createRange();a.setStart(left,left.length-1);a.setEnd(left,left.length);
        const b=document.createRange();b.setStart(right,0);b.setEnd(right,right.data.codePointAt(0)>65535?2:1);
        const first=a.getBoundingClientRect(),last=b.getBoundingClientRect(),box=e.getBoundingClientRect();
        return {prefix:left.data,suffix:right.data,inside:e.parentNode===h,
            selection:e.contains(s.anchorNode)&&s.isCollapsed,
            sameLine:Math.abs(first.top-last.top)<2,
            caret:box.height>0&&Math.abs(box.left-first.right)<2&&Math.abs(box.right-last.left)<2};''')
    assert result==dict(prefix=prefix,suffix=suffix,inside=True,selection=True,sameLine=True,caret=True),result
    kind='offline' if browser.js('return location.protocol;')=='file:' else 'playground'
    Path(f'build/output-cursor-{browser.kind}-{kind}.png').write_bytes(base64.b64decode(browser.command('GET','/screenshot')))


def verify_output(browser):
    """Verify display controls across valid chunk boundaries without changing raw I/O."""
    import json
    cases=[c for c in json.loads(Path(__file__).with_name('cases.json').read_text())
           if c['name'].startswith('console_output_')]
    result=browser.page_async('''const cases=arguments[0],done=arguments[arguments.length-1];
        const view=document.createElement('pre'),history=document.createElement('span'),editor=document.createElement('span');
        view.style.cssText='width:160px;white-space:pre-wrap;overflow-wrap:anywhere;font:16px monospace';
        view.append(history,editor);document.body.append(view);
        const renderer=createPas2WasmTerminal(view,history,editor,()=>true);
        function check(value,message){if(!value)throw Error(message);}
        const output=()=>Array.from(history.childNodes).filter(n=>n!==editor).map(n=>n.textContent).join('');
        try{
            for(const c of cases){
                const scalars=Array.from(c.output);
                // Whole chunks, every valid scalar boundary, and one scalar per chunk.
                const divisions=[[c.output],scalars];
                for(let at=0;at<=scalars.length;at++)divisions.push([scalars.slice(0,at).join(''),scalars.slice(at).join('')]);
                for(const chunks of divisions){
                    renderer.clear();for(const text of chunks)renderer.append(text);
                    check(output()===c.display,c.name+': wrong display '+JSON.stringify(output()));
                    renderer.waiting(true,'key');
                    const line=c.display.lastIndexOf('\\n')+1;
                    const expectedPrefix=c.display.slice(0,line)+Array.from(c.display.slice(line)).slice(0,c.displayCursor).join('');
                    check(history.firstChild.data===expectedPrefix,c.name+': wrong output cursor');
                    check(document.activeElement===editor,c.name+': caret lost focus');
                    renderer.waiting(false);check(history.textContent===c.display,c.name+': completion lost output');
                }
            }
            // An unchanged selection must survive moving text across the cursor.
            renderer.clear();renderer.append('abc');renderer.waiting(true,'key');
            const selected=document.createRange(),selection=window.getSelection();
            selected.setStart(history.firstChild,1);selected.setEnd(history.firstChild,3);
            selection.removeAllRanges();selection.addRange(selected);renderer.append('\\b');
            check(selection.toString()==='bc','History copy changed after cursor movement');
            renderer.append('C');check(output()==='abC','Overwrite after selection');
            // Native draft nodes/selection/focus survive both editor reparentings.
            renderer.clear();renderer.append('Prompt: ');renderer.waiting(true);editor.textContent='ab😀cd';
            const draft=editor.firstChild;selection.setBaseAndExtent(draft,1,draft,5);
            renderer.append('abc\\b');
            check(document.activeElement===editor&&selection.anchorNode===draft&&selection.anchorOffset===1&&selection.focusOffset===5,
                'Draft lost on backspace output');
            renderer.append('Z');
            check(editor.parentNode===view&&document.activeElement===editor&&selection.anchorNode===draft&&selection.toString()==='b😀c',
                'Draft lost when overwriting the suffix');
            // A large control chunk is processed as a chunk, not quadratic DOM edits.
            renderer.clear();renderer.append('a'.repeat(65536));renderer.append('\\b'.repeat(65536)+'Z');
            check(output()==='Z'+'a'.repeat(65535),'Large control chunk');
            renderer.clear();renderer.append('Z');check(output()==='Z','Fresh cursor state');
            // Hidden/replaced output bytes never replenish the actual runtime budget.
            renderer.clear();let raw='';const runtime=createPas2WasmRuntime('',text=>{raw+=text;renderer.append(text);},{output:4});
            for(const value of [97,8,98,8])runtime.imports.write_char(value,1,1);
            let code;try{runtime.imports.write_char(99,1,1);}catch(error){code=error.code;}
            check(code==='R_OUTPUT_LIMIT'&&raw==='a\\bb\\b'&&output()==='b','Output allowance was refunded');
            check(!view.querySelector('img'),'Output was interpreted as markup');
            done({ok:true,cases:cases.length});
        }catch(error){done({error:error.stack});}finally{view.remove();}''',cases)
    assert result==dict(ok=True,cases=len(cases)),result
