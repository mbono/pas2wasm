# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Raw-key JSPI workers and the shared browser/offline terminal, without Node."""
import base64
import json
from pathlib import Path
import interactive
import terminal


def fixtures():
    """Load key chunks, expected requests, and equivalent closed input."""
    cases = json.loads(Path(__file__).with_suffix('.json').read_text())
    shared = json.loads(Path(__file__).with_name('cases.json').read_text())
    for case in cases:
        if 'sourceCase' in case:
            case['source'] = next(c['source'] for c in shared if c['name']==case['sourceCase'])
    return cases


def verify_workers(browser, compile_native):
    """Compare fresh batch/live workers and reject stale/duplicate key replies."""
    cases = fixtures()
    artifacts = [compile_native(c['source'], 'key-'+c['name']) for c in cases]
    result = browser.page_async('''const cases=arguments[0],artifacts=arguments[1],done=arguments[arguments.length-1];
        (async()=>{
            async function execute(artifact,test,live,limits,disableJspi=false) {
                const prefix=disableJspi?"Object.defineProperty(WebAssembly,'Suspending',{value:undefined});":'';
                const url=URL.createObjectURL(new Blob([prefix+pas2WasmWorkerSource()],{type:'text/javascript'}));
                const worker=new Worker(url);URL.revokeObjectURL(url);
                try {return await new Promise((resolve,reject)=>{
                    let output='',cursor=0;const requests=[];
                    const timer=setTimeout(()=>reject(Error(test.name+': key worker timeout')),15000);
                    worker.onerror=e=>{clearTimeout(timer);reject(Error(e.message));};
                    worker.onmessage=({data})=>{
                        if(data.type==='output')output+=data.text;
                        if(data.type==='input-request'){
                            const step=test.steps[cursor++];requests.push({kind:data.kind,prefix:data.prefix});
                            if(!step || step.kind!==data.kind || (step.prefix||'')!==data.prefix){
                                clearTimeout(timer);reject(Error(test.name+': incorrect request '+JSON.stringify(data)));return;
                            }
                            const reply={type:step.text===null?'end-input':'input',id:1,requestId:data.requestId,text:step.text};
                            worker.postMessage({...reply,id:999,text:'stale'});
                            worker.postMessage({...reply,requestId:data.requestId+100,text:'stale'});
                            worker.postMessage(reply);worker.postMessage(reply);
                        }
                        if(data.type==='done'||data.type==='error'){
                            clearTimeout(timer);resolve({output,code:data.diagnostic?.code,requests});
                        }
                    };
                    worker.postMessage({type:'run',id:1,binary:Uint8Array.from(artifact.binary),
                        interactive:live,input:live?'':test.input,limits});
                });}finally{worker.terminate();}
            }
            const results=[];
            for(let i=0;i<cases.length;i++){
                const live=await execute(artifacts[i],cases[i],true),batch=await execute(artifacts[i],cases[i],false);
                if(live.output!==batch.output||live.code!==batch.code)throw Error(cases[i].name+': key live/batch mismatch');
                results.push(live);
            }
            const prefix=cases.findIndex(c=>c.name==='prefix'),chunk=cases.findIndex(c=>c.name==='chunk');
            const exact=await execute(artifacts[prefix],cases[prefix],true,{input:6});
            const limited=await execute(artifacts[prefix],cases[prefix],true,{input:5});
            const chunkLimit=await execute(artifacts[chunk],cases[chunk],true,{input:1});
            const unavailable=await execute(artifacts[0],cases[0],true,undefined,true);
            const fallback=await execute(artifacts[0],cases[0],false,undefined,true);
            done({results,exact,limited,chunkLimit,unavailable,fallback});
        })().catch(e=>done({error:e.stack}));''', cases, artifacts)
    assert not result.get('error'), result
    for case, actual in zip(cases, result['results'], strict=True):
        assert actual['output'] == case['output'], (case['name'],actual)
        assert actual.get('code') == case.get('runtime'), (case['name'],actual)
        assert len(actual['requests']) == len(case['steps']), (case['name'],actual)
    assert result['exact'].get('code') is None, result
    assert result['limited']['code'] == result['chunkLimit']['code'] == 'R_INPUT_LIMIT', result
    assert result['unavailable']['code'] == 'E_JSPI', result
    assert result['fallback']['output'] == cases[0]['output'] and not result['fallback'].get('code'),result
    scalar = compile_native('program p;\nuses crt;\nbegin\n writeln(ord(readkey))\nend.', 'key-scalar')
    helper = Path('test/keyboard-runtime.js').read_text()
    boundary = browser.page_async(helper+'''const binary=Uint8Array.from(arguments[0]),done=arguments[arguments.length-1];
        (async()=>{await verifyKeyboardRuntime(createPas2WasmRuntime);
            await verifyCrtScalarBoundary(binary,createPas2WasmRuntime);done({ok:true});
        })().catch(e=>done({error:e.stack}));''', scalar['binary'])
    assert boundary == {'ok':True}, boundary
    verify_session_budget(browser)
    verify_composition_boundary(browser)
    for case, artifact in zip(cases, artifacts, strict=True):
        if any(step['text'] is None for step in case['steps']):
            interactive.verify_session_case(browser, case, artifact['binary'])
    return cases


def verify_composition_boundary(browser):
    """A late IME insertion/Enter must not answer the next worker request."""
    result=browser.page_async('''const done=arguments[arguments.length-1];
        (async()=>{
            const view=document.createElement('pre'),history=document.createElement('span'),editor=document.createElement('span');
            view.append(history,editor);document.body.append(view);const replies=[];
            const renderer=createPas2WasmTerminal(view,history,editor,text=>{
                replies.push(text);renderer.waiting(false);return true;
            });
            try{
                renderer.waiting(true,'key');
                editor.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));
                editor.textContent='é';
                editor.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',isComposing:true,bubbles:true}));
                editor.dispatchEvent(new CompositionEvent('compositionend',{data:'é',bubbles:true}));
                await new Promise(r=>setTimeout(r,10));renderer.waiting(true,'key');
                editor.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertText',data:'é',bubbles:true,cancelable:true}));
                editor.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertParagraph',bubbles:true,cancelable:true}));
                const beforeRelease=replies.slice();
                editor.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter',bubbles:true}));
                editor.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true,cancelable:true}));
                done({beforeRelease,replies,history:history.textContent});
            }finally{view.remove();}
        })().catch(e=>done({error:e.stack}));''')
    assert result == {'beforeRelease':['é'],'replies':['é','\r'],'history':''},result


def verify_session_budget(browser):
    """Credit a near-limit raw prefix exactly once in the page's session guard."""
    result=browser.page_async('''const done=arguments[arguments.length-1],original=window.pas2WasmWorkerSource;
        window.pas2WasmWorkerSource=()=>`let id,n=0;onmessage=({data})=>{
            if(data.type==='run')id=data.id;
            postMessage({type:'running',id});
            if(++n===1)postMessage({type:'input-request',id,requestId:n,kind:'key'});
            else if(n===2)postMessage({type:'input-request',id,requestId:n,kind:'line',prefix:'a'.repeat(1048574)});
            else postMessage({type:'input-request',id,requestId:n,kind:'key'});
        };`;
        let submits=0,diagnostic;
        const session=createPas2WasmSession({output(){},error(d){diagnostic=d;},state(s){
            if(s==='Failed')done({submits,diagnostic});
        },waiting(value,request){if(value){submits++;
            const text=submits===1?'a'.repeat(1048575):submits===2?request.prefix:'x';
            session.submit(text);
        }}},5000);
        try{session.run(new Uint8Array(),'',true);}finally{window.pas2WasmWorkerSource=original;}''')
    assert result['submits']==3 and result['diagnostic']['code']=='R_INPUT_LIMIT',result


def paste(browser, text):
    """Deliver plain text through the production paste listener in the page realm."""
    browser.page_async('''const data=new DataTransfer();data.setData('text/plain',arguments[0]);
        data.setData('text/html','<img src=x>');const event=new Event('paste',{bubbles:true,cancelable:true});
        Object.defineProperty(event,'clipboardData',{value:data});
        document.getElementById('console-input').dispatchEvent(event);arguments[arguments.length-1](true);''', text)


def verify_console_case(browser, case):
    """Use real keys/paste/IME; EOF fixtures check the waiting editor then Stop."""
    interactive.select_mode(browser,'interactive')
    # Observe actual worker requests; each next action must wait for a new request,
    # even when consecutive reads have identical mode/status/output.
    browser.page_async('''window.keyRequests=[];const NativeWorker=window.Worker;
        window.Worker=function(...args){const worker=new NativeWorker(...args);
            worker.addEventListener('message',({data})=>{if(data.type==='input-request')window.keyRequests.push(data);});return worker;};
        window.restoreKeyWorker=()=>{window.Worker=NativeWorker;delete window.restoreKeyWorker;};
        arguments[arguments.length-1](true);''')
    try:
        browser.click('run')
        for index, step in enumerate(case['steps']):
            browser.wait(f'window.keyRequests.length==={index+1}')
            browser.wait("document.getElementById('status').textContent==='Waiting for input'")
            request = browser.js('return window.keyRequests[arguments[0]];', index)
            assert request['kind'] == step['kind'] and request['prefix'] == step.get('prefix',''), (case['name'],request)
            assert browser.js('return document.activeElement.id;') == 'console-input'
            label = 'Console key input' if step['kind']=='key' else 'Console input line'
            assert browser.js("return document.getElementById('console-input').getAttribute('aria-label');") == label
            if 'cursorPrefix' in step:
                terminal.verify_cursor(browser,step['cursorPrefix'],step['cursorSuffix'])
            if step.get('prefix'):
                assert browser.js("return document.getElementById('console-input').textContent;") == step['prefix']
                assert browser.js("const s=window.getSelection(),e=document.getElementById('console-input');return s.isCollapsed&&e.contains(s.anchorNode);")
            if step['text'] is None:
                # EOF is programmatic only; retain caret/draft/cancellation UI checks.
                output = browser.js("return document.getElementById('output').textContent;")
                if step.get('draft'):
                    terminal.keys(browser,step['draft'])
                assert browser.js("return !document.querySelector('#console-eof, .p2w-console-controls, .p2w-console button');")
                browser.click('stop')
                browser.wait("document.getElementById('status').textContent==='Stopped'")
                assert browser.js("return document.getElementById('output').textContent;") == output
                assert browser.js("const e=document.getElementById('console-input');return e.textContent===''&&e.getAttribute('aria-disabled')==='true';")
                return
            elif step['kind']=='line':
                # Append to the existing prefix with native keyboard editing.
                terminal.keys(browser,step.get('editKeys',step['text'][len(step.get('prefix','')):]+'\ue007'))
            elif step.get('composition'):
                browser.page_async('''const e=document.getElementById('console-input'),text=arguments[0];
                    e.dispatchEvent(new CompositionEvent('compositionstart',{bubbles:true}));e.textContent=text;
                    e.dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',isComposing:true,bubbles:true}));
                    e.dispatchEvent(new CompositionEvent('compositionend',{data:text,bubbles:true}));
                    e.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertFromComposition',data:text,bubbles:true,cancelable:true}));
                    e.dispatchEvent(new InputEvent('beforeinput',{inputType:'insertParagraph',bubbles:true,cancelable:true}));
                    e.dispatchEvent(new KeyboardEvent('keyup',{key:'Enter',bubbles:true}));
                    arguments[arguments.length-1](true);''', step['text'])
            elif step.get('keys'):
                if index == 0:
                    # Navigation/modifiers must neither consume a key nor alter history.
                    terminal.keys(browser,'\ue012\ue014\ue011\ue010')
                    assert browser.js('return window.keyRequests.length;') == 1
                    assert browser.js("const e=document.getElementById('console-input');return e.getBoundingClientRect().height>0&&getComputedStyle(e).caretColor!=='transparent';")
                    suffix='offline' if browser.js('return location.protocol;')=='file:' else 'playground'
                    Path(f'build/keyboard-{browser.kind}-{suffix}.png').write_bytes(base64.b64decode(browser.command('GET','/screenshot')))
                terminal.keys(browser,step['keys'])
            else:
                paste(browser,step['text'])
        expected = 'Failed' if case.get('runtime') else 'Completed'
        browser.wait(f"document.getElementById('status').textContent==={json.dumps(expected)}")
        assert browser.js("return document.getElementById('output').textContent;") == case['transcript'],case['name']
        assert browser.js("return document.getElementById('console-input').textContent;") == '',case['name']
        assert browser.js("return document.querySelectorAll('img').length;") == 0
        diagnostic=browser.js("return document.getElementById('diagnostics').textContent;")
        assert (case['runtime'] in diagnostic) if case.get('runtime') else not diagnostic,diagnostic
    finally:
        browser.page_async('window.restoreKeyWorker();arguments[arguments.length-1](true);')


def verify_ui(browser):
    """Exercise each keyboard program in the playground and recover after Stop."""
    for case in fixtures():
        browser.js("const s=document.getElementById('source');s.value=arguments[0];s.dispatchEvent(new Event('input'));",case['source'])
        if case['name']=='controls':
            verify_controls(browser)
        if case['name']=='output_cursor':
            verify_cursor_stop(browser)
        verify_console_case(browser,case)
    browser.js("const s=document.getElementById('source');s.value=arguments[0];s.dispatchEvent(new Event('input'));",spin_source())
    verify_timeout(browser)


def spin_source():
    """Resume from one real key wait into a noncooperative computation."""
    return 'program p; uses crt; var c:char; begin c:=readkey; while true do begin end end.'


def verify_cursor_stop(browser):
    """Cancelling a wait behind line end preserves output and detaches the editor."""
    interactive.select_mode(browser,'interactive')
    for mode in ('stop','batch'):
        browser.click('run')
        browser.wait("document.getElementById('status').textContent==='Waiting for input'")
        terminal.verify_cursor(browser,'ab','c')
        if mode=='stop':
            browser.click('stop')
            assert browser.js("return document.getElementById('status').textContent;")=='Stopped'
        else:
            interactive.select_mode(browser,'batch')
        assert browser.js("const e=document.getElementById('console-input');return e.parentNode.id==='terminal'&&e.getAttribute('aria-disabled')==='true';")
        assert browser.js("return document.getElementById('output').textContent;")=='abc'
    interactive.select_mode(browser,'interactive')


def verify_timeout(browser):
    """Charge resumed CPU work and discard the worker after its active budget."""
    interactive.select_mode(browser,'interactive')
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    terminal.keys(browser,'x')
    browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'R_TIMEOUT' in browser.js("return document.getElementById('diagnostics').textContent;")


def verify_controls(browser):
    """Keep long key waits free, discard cancellations, and bound un-echoed chunks."""
    interactive.select_mode(browser,'interactive')
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    browser.page_async('const done=arguments[arguments.length-1];setTimeout(()=>done(true),6100);')
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    # Shift+Tab must retain an accessible path out while plain Tab is a Pascal key.
    terminal.keys(browser,'\ue008\ue004\ue000')
    previous=browser.js('return document.activeElement.id;')
    assert previous and previous not in ('console-input','terminal'),previous
    terminal.keys(browser,'\ue004',previous)
    assert browser.js('return document.activeElement.id;') == 'console-input'
    browser.click('stop')
    assert browser.js("return document.getElementById('status').textContent;") == 'Stopped'
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    interactive.select_mode(browser,'batch')
    assert browser.js("return document.getElementById('console-input').getAttribute('aria-disabled');") == 'true'
    interactive.select_mode(browser,'interactive')
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    paste(browser,'a'*1048577)
    browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'R_INPUT_LIMIT' in browser.js("return document.getElementById('diagnostics').textContent;")
    assert browser.js("return document.getElementById('output').textContent;") == 'Keys: '
    interactive.verify_clock(browser,'key')
