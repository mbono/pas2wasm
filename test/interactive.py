# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Live worker, console and offline checks using the production JSPI host."""
import json
from pathlib import Path


def fixtures():
    """Load shared live-input programs without adding a Node dependency."""
    return json.loads(Path(__file__).with_name('interactive.json').read_text())


def submit(browser, text, keyboard=False):
    """Submit the enabled editor, optionally using a real Enter key event."""
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    assert browser.js("return document.activeElement.id;") == 'console-input'
    browser.js("const input=document.getElementById('console-input');input.textContent=arguments[0];window.getSelection().collapse(input,input.childNodes.length);", text)
    if keyboard:
        element = browser.command('POST', '/element', {'using':'css selector','value':'#console-input'})
        reference = element['element-6066-11e4-a52e-4f735466cecf']
        browser.command('POST', f'/element/{reference}/value', {'text':'\ue007'})
    else:
        browser.js("document.getElementById('console-input').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));")


def verify_console_case(browser, case, start=True):
    """Drive one real console run, including echo, EOF and inert markup."""
    if case['name'] == 'invalid_unicode':
        return
    if start:
        browser.js("document.getElementById('run').click();")
    for line in case['lines']:
        submit(browser, line)
    if case.get('runtime') == 'R_INPUT_EOF':
        browser.wait("document.getElementById('status').textContent==='Waiting for input'")
        browser.click('console-eof')
    expected = 'Failed' if case.get('runtime') else 'Completed'
    browser.wait(f"document.getElementById('status').textContent==={json.dumps(expected)}")
    assert browser.js("return document.getElementById('output').textContent;") == case['transcript'],case['name']
    diagnostic = browser.js("return document.getElementById('diagnostics').textContent;")
    assert (case['runtime'] in diagnostic) if case.get('runtime') else not diagnostic, (case['name'],diagnostic)
    assert browser.js("return document.querySelectorAll('img').length;") == 0
    assert browser.js("return document.getElementById('console-input').getAttribute('aria-disabled')==='true';")


def select_mode(browser, mode):
    """Use the production mode-change handler rather than private page state."""
    browser.js("const mode=document.getElementById('io-mode'); mode.value=arguments[0]; mode.dispatchEvent(new Event('change'));",mode)


def verify_workers(browser, compile_native, legacy):
    """Compare live/closed input in actual workers, including stale input rejection."""
    examples = fixtures()
    artifacts = [compile_native(c['source'],'live-'+c['name']) for c in examples]
    large = compile_native(limit_source(),'live-input-limit')
    output_limit = compile_native('program demo; var i:integer; begin for i:=1 to 300000 do writeln(1234) end.','live-output-limit')
    wire_examples = examples + [dict(name='legacy', lines=['12 ignored','1.25 junk','érest','hello','skip','7'],output='12\n1.25\né\nhello\n7\n')]
    result = browser.page_async('''const cases=arguments[0],artifacts=arguments[1],large=arguments[2],outputLimit=arguments[3],done=arguments[arguments.length-1];
        (async()=>{
            async function execute(artifact,test,interactive,prefix='') {
                const url=URL.createObjectURL(new Blob([prefix+pas2WasmWorkerSource()],{type:'text/javascript'}));
                const worker=new Worker(url);URL.revokeObjectURL(url);
                try { return await new Promise((resolve,reject)=>{
                    let output='',cursor=0,requests=0;
                    const timeout=setTimeout(()=>reject(Error('worker timeout')),15000);
                    worker.onerror=e=>{clearTimeout(timeout);reject(Error(e.message));};
                    worker.onmessage=({data})=>{
                        if(data.type==='output') output+=data.text;
                        else if(data.type==='capabilities' && interactive && !data.jspi && !prefix) {clearTimeout(timeout);reject(Error('JSPI required'));}
                        else if(data.type==='input-request') {
                            requests++;
                            worker.postMessage({type:'input',id:999,requestId:data.requestId,text:'stale'});
                            worker.postMessage({type:'input',id:1,requestId:data.requestId+100,text:'stale'});
                            const text=cursor<test.lines.length?test.lines[cursor++]:null;
                            setTimeout(()=>worker.postMessage({type:text===null?'end-input':'input',id:1,requestId:data.requestId,text}),1);
                        } else if(data.type==='done'||data.type==='error') {
                            clearTimeout(timeout);resolve({output,diagnostic:data.diagnostic||null,requests});
                        }
                    };
                    worker.postMessage({type:'run',id:1,binary:Uint8Array.from(artifact.binary),interactive,
                        input:interactive?'':test.lines.map(line=>line+'\\n').join('')});
                }); } finally {worker.terminate();}
            }
            const results=[];
            for(let i=0;i<cases.length;i++) {
                const live=await execute(artifacts[i],cases[i],true);
                const batch=await execute(artifacts[i],cases[i],false);
                if(JSON.stringify({output:live.output,diagnostic:live.diagnostic})!==JSON.stringify({output:batch.output,diagnostic:batch.diagnostic})) throw Error(cases[i].name+': live/batch mismatch');
                results.push(live);
            }
            const prefix="Object.defineProperty(WebAssembly,'Suspending',{value:undefined});";
            const unavailable=await execute(artifacts[0],cases[0],true,prefix);
            // The forced-unsupported case has no input request, so compare closed input separately.
            const fallback=await execute(artifacts[0],cases[0],false,prefix);
            const inputLimited=await execute(large,{lines:['é'.repeat(262144),'é'.repeat(262144)]},true);
            const outputLimited=await execute(outputLimit,{lines:[]},true);
            done({results,unavailable,fallback,inputLimited:inputLimited.diagnostic,
                outputLimited:outputLimited.diagnostic,outputBytes:new TextEncoder().encode(outputLimited.output).length});
        })().catch(error=>done({error:error.message}));''', wire_examples, artifacts+[legacy], large, output_limit)
    assert not result.get('error'),result
    for case, actual in zip(wire_examples, result['results'], strict=True):
        assert actual['output'] == case['output'],(case['name'],actual)
        assert (actual['diagnostic']['code'] if actual['diagnostic'] else None) == case.get('runtime'),(case['name'],actual)
        if not case.get('runtime'):
            assert actual['requests'] == len(case['lines'])
    assert result['unavailable']['diagnostic']['code'] == 'E_JSPI'
    assert result['fallback']['output'] == examples[0]['output']
    assert result['inputLimited']['code'] == 'R_INPUT_LIMIT',result
    assert result['outputLimited']['code'] == 'R_OUTPUT_LIMIT',result
    assert result['outputBytes'] <= 1048576
    return examples


def verify_clock(browser):
    """Exercise the production session budget using real worker busy intervals.

    A protocol fixture supplies three 140ms active segments and two 500ms waits.
    Each active segment fits 350ms, while their sum must exceed it. This detects
    both charging human waits and replenishing the budget on each resume.
    """
    result = browser.page_async('''const done=arguments[arguments.length-1];
        const original=window.pas2WasmWorkerSource;
        window.pas2WasmWorkerSource=()=>`let id,request=0;onmessage=({data})=>{
            if(data.type==='run')id=data.id;
            postMessage({type:'running',id});
            const end=performance.now()+140;while(performance.now()<end){}
            if(++request<3)postMessage({type:'input-request',id,requestId:request});
            else postMessage({type:'done',id});
        };`;
        let waits=0,diagnostic=null;
        const session=createPas2WasmSession({output(){},error(d){diagnostic=d;},state(state){
            if(state==='Waiting for input'){waits++;setTimeout(()=>session.submit('x'),500);}
            if(state==='Failed'||state==='Completed')done({state,diagnostic,waits});
        }},350);
        try{session.run(new Uint8Array(),'',true);}finally{window.pas2WasmWorkerSource=original;}
        ''')
    assert result['state'] == 'Failed' and result['diagnostic']['code'] == 'R_TIMEOUT',result
    assert result['waits'] == 2,result


def verify_ui(browser):
    """Check Enter, waiting over five seconds, Stop, EOF and unsupported recovery."""
    example = fixtures()[0]
    browser.js("const source=document.getElementById('source');source.value=arguments[0];source.dispatchEvent(new Event('input'));",example['source'])
    select_mode(browser,'interactive')
    assert browser.js("return document.getElementById('batch-input').hidden;")
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    assert browser.js("return document.getElementById('output').textContent;") == 'First: '
    browser.page_async("const done=arguments[arguments.length-1];setTimeout(()=>done(true),6100);")
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    submit(browser,'2',keyboard=True)
    submit(browser,'3')
    browser.wait("document.getElementById('status').textContent==='Completed'")
    assert browser.js("return document.getElementById('output').textContent;") == example['transcript']
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    browser.click('stop')
    assert browser.js("return document.getElementById('status').textContent;") == 'Stopped'
    assert browser.js("return document.getElementById('console-input').getAttribute('aria-disabled')==='true';")
    verify_console_case(browser,example)
    browser.js("const source=document.getElementById('source');source.value='program spin; begin while true do begin end end.';source.dispatchEvent(new Event('input'));document.getElementById('run').click();")
    browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'R_TIMEOUT' in browser.js("return document.getElementById('diagnostics').textContent;")
    browser.js("const source=document.getElementById('source');source.value=arguments[0];source.dispatchEvent(new Event('input'));",limit_source())
    verify_input_limit(browser)
    browser.js("const source=document.getElementById('source');source.value=arguments[0];source.dispatchEvent(new Event('input'));",example['source'])
    verify_fallback(browser,example)



def verify_fallback(browser, case):
    """Force a missing JSPI API and recover using closed batch input."""
    select_mode(browser,'interactive')
    browser.page_async("window.savedWorkerSource=window.pas2WasmWorkerSource;window.pas2WasmWorkerSource=()=>\"Object.defineProperty(WebAssembly,'Suspending',{value:undefined});\"+window.savedWorkerSource();arguments[arguments.length-1](true);")
    try:
        browser.click('run')
        browser.wait("document.getElementById('status').textContent==='Failed'")
        assert 'E_JSPI' in browser.js("return document.getElementById('diagnostics').textContent;")
        assert 'Batch input' in browser.js("return document.getElementById('io-note').textContent;")
    finally:
        browser.page_async("window.pas2WasmWorkerSource=window.savedWorkerSource;delete window.savedWorkerSource;arguments[arguments.length-1](true);")
    select_mode(browser,'batch')
    browser.js("document.getElementById('input').value=arguments[0];document.getElementById('run').click();", ''.join(line+'\n' for line in case['lines']))
    browser.wait("document.getElementById('status').textContent==='Completed'")
    assert browser.js("return document.getElementById('output').textContent;") == case['output']


def verify_offline_controls(browser, case):
    """Verify the exported controller excludes human waits and cancels safely."""
    browser.click('run')
    browser.wait("document.getElementById('status').textContent==='Waiting for input'")
    browser.page_async("const done=arguments[arguments.length-1];setTimeout(()=>done(true),6100);")
    assert browser.js("return document.getElementById('status').textContent;") == 'Waiting for input'
    browser.click('stop')
    assert browser.js("return document.getElementById('status').textContent;") == 'Stopped'
    verify_console_case(browser,case)
    verify_clock(browser)
    verify_fallback(browser,case)


def limit_source():
    """Use global string storage so input-byte tests do not exhaust the frame."""
    return 'program demo; var s:string[524288]; begin readln(s); readln(s) end.'


def verify_input_limit(browser):
    """Two UTF-8 submissions exceed 1 MiB; only the accepted first line is echoed."""
    select_mode(browser,'interactive')
    browser.click('run')
    for index in range(2):
        browser.wait("document.getElementById('status').textContent==='Waiting for input'")
        if index:
            assert browser.js("const output=document.getElementById('terminal');return output.scrollHeight<=output.clientHeight || output.scrollTop>0;")
        browser.js("document.getElementById('console-input').textContent='é'.repeat(262144);document.getElementById('console-input').dispatchEvent(new KeyboardEvent('keydown',{key:'Enter',bubbles:true}));")
    browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'R_INPUT_LIMIT' in browser.js("return document.getElementById('diagnostics').textContent;")
    assert browser.js("return document.getElementById('output').textContent==='é'.repeat(262144)+'\\n';")
    select_mode(browser,'batch')
    browser.js("document.getElementById('input').value=arguments[0];document.getElementById('run').click();",'ok\nok\n')
    browser.wait("document.getElementById('status').textContent==='Completed'")
    assert not browser.js("return document.getElementById('output').textContent;")
