# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Text-file worker, real selection/download and standalone-export acceptance."""
import json
import base64
from pathlib import Path
import time
import interactive
import keyboard


def fixtures():
    """Keep program/source/input/result expectations shared with Node."""
    cases=json.loads(Path(__file__).with_name('file-transfer.json').read_text())
    shared=json.loads(Path(__file__).with_name('cases.json').read_text())
    for case in cases:
        if 'sourceCase' in case:
            case['source']=next(c['source'] for c in shared if c['name']==case['sourceCase'])
    return cases


def contents(file):
    """Encode fixture text without normalizing BOM, CRLF or control characters."""
    return (file['text']*file.get('repeat',1)).encode('utf-8')


def expected_files(files):
    """Use wire-byte arrays so browser and native/WAT checks compare exact contents."""
    return [dict(name=f['name'],data=list(contents(f))) for f in files]


def verify_workers(browser, compile_native):
    """Actual JSPI/batch workers publish results only with successful completion."""
    cases=fixtures()
    artifacts=[compile_native(c['source'],'transfer-'+c['name']) for c in cases]
    result=browser.page_async('''const cases=arguments[0],artifacts=arguments[1],done=arguments[arguments.length-1];
        (async()=>{
            async function execute(binary,test,live){
                const url=URL.createObjectURL(new Blob([pas2WasmWorkerSource()],{type:'text/javascript'}));
                const worker=new Worker(url);URL.revokeObjectURL(url);
                try{return await new Promise((resolve,reject)=>{
                    let output='',next=0;const timer=setTimeout(()=>reject(Error('file worker timeout')),15000);
                    worker.onerror=e=>{clearTimeout(timer);reject(Error(e.message));};
                    worker.onmessage=({data})=>{
                        if(data.type==='output')output+=data.text;
                        if(data.type==='input-request'){
                            const step=(test.steps||test.lines?.map(text=>({kind:'line',text}))||[])[next++];
                            if(step&&step.kind!==data.kind){clearTimeout(timer);reject(Error('Wrong request kind'));return;}
                            const text=step?.text??null;
                            worker.postMessage({type:'input',id:999,requestId:data.requestId,text:'stale'});
                            worker.postMessage({type:text===null?'end-input':'input',id:1,requestId:data.requestId,text});
                        }
                        if(data.type==='done'||data.type==='error'){
                            clearTimeout(timer);resolve({output,code:data.diagnostic?.code||null,requests:next,
                                files:(data.files||[]).map(f=>({name:f.name,data:Array.from(f.data)}))});
                        }
                    };
                    worker.postMessage({type:'run',id:1,binary:Uint8Array.from(binary),interactive:live,input:live?'':test.input||'',
                        files:test.files.map(f=>({name:f.name,data:new TextEncoder().encode(f.text.repeat(f.repeat??1))}))});
                });}finally{worker.terminate();}
            }
            const results=[];
            for(let i=0;i<cases.length;i++){
                const batch=await execute(artifacts[i].binary,cases[i],false),live=await execute(artifacts[i].binary,cases[i],true);
                results.push({batch,live});
            }
            done({results});
        })().catch(e=>done({error:e.stack}));''',cases,artifacts)
    assert not result.get('error'),result
    for case,actual in zip(cases,result['results'],strict=True):
        for mode,value in actual.items():
            assert value['output']==case['output'] and value['code']==case.get('runtime'),(case['name'],mode,value)
            assert value['files']==expected_files(case['resultFiles']),(case['name'],mode,value)
            assert value['requests']==(len(case.get('steps',case.get('lines',[]))) if mode=='live' else 0),(case['name'],mode,value)
    verify_boundaries(browser)
    verify_session(browser)
    return cases


def verify_boundaries(browser):
    """Use the same malformed metadata, ownership and pending-read checks offline."""
    checks=Path('test/file-transfer-runtime.js').read_text()+Path('test/file-transfer-ui.js').read_text()
    result=browser.page_async(checks+'''const done=arguments[arguments.length-1];
        (async()=>{verifyFileTransferRuntime(createPas2WasmRuntime);await verifyFileTransferUI();done({ok:true});})()
            .catch(e=>done({error:e.stack}));''')
    assert result=={'ok':True},result


def verify_session(browser):
    """Ignore stale/duplicate done snapshots and retain caller-owned input bytes."""
    result=browser.page_async('''const done=arguments[arguments.length-1],original=window.pas2WasmWorkerSource;
        window.pas2WasmWorkerSource=()=>`onmessage=({data})=>{
            postMessage({type:'done',id:data.id+99,files:[{name:'stale',data:new Uint8Array([0])}]});
            postMessage({type:'done',id:data.id,files:data.files});
            postMessage({type:'done',id:data.id,files:[{name:'duplicate',data:new Uint8Array([0])}]});
        };`;
        const snapshots=[],bytes=new Uint8Array([120]);let completed=0;
        const session=createPas2WasmSession({output(){},error(e){done({error:e});},files(f){snapshots.push(f.map(v=>v.name));},
            state(s){if(s==='Completed'){completed++;setTimeout(()=>done({snapshots,completed,byte:bytes[0]}),30);}}});
        try{session.run(new Uint8Array(),'',false,[{name:'current',data:bytes}]);}
        finally{window.pas2WasmWorkerSource=original;}''')
    assert result==dict(snapshots=[['current']],completed=1,byte=120),result


def select_files(browser, files, directory):
    """Use the real file input, not a synthetic upload implementation."""
    if not browser.js("return document.getElementById('clear-files').disabled;"):
        browser.click('clear-files')
    if not files:
        return
    directory.mkdir(exist_ok=True)
    paths=[]
    for file in files:
        path=directory/file['name'];path.write_bytes(contents(file));paths.append(str(path))
    element=browser.command('POST','/element',{'using':'css selector','value':'#input-files'})
    reference=element['element-6066-11e4-a52e-4f735466cecf']
    browser.command('POST',f'/element/{reference}/value',{'text':'\n'.join(paths)})
    browser.wait(f"document.querySelectorAll('#input-file-list li').length==={len(files)}&&!document.getElementById('input-files').disabled")
    assert not browser.js("return document.getElementById('file-status').textContent;")


def verify_case(browser, case, mode):
    """Run a selected program and inspect/download its successful result snapshots."""
    interactive.select_mode(browser,mode)
    browser.js("document.getElementById('input').value=arguments[0];",case.get('input',''))
    browser.click('run')
    if mode=='interactive':
        for step in case.get('steps',[]):
            browser.wait("document.getElementById('status').textContent==='Waiting for input'")
            keyboard.paste(browser,step['text'])
        for text in case.get('lines',[]):
            interactive.submit(browser,text,keyboard=True)
    state='Failed' if case.get('runtime') else 'Completed'
    browser.wait(f"document.getElementById('status').textContent==={json.dumps(state)}")
    expected=case.get('transcript',case.get('display',case['output'])) if mode=='interactive' else case.get('display',case['output'])
    assert browser.js("return document.getElementById('output').textContent;")==expected,case['name']
    diagnostic=browser.js("return document.getElementById('diagnostics').textContent;")
    assert (case['runtime'] in diagnostic) if case.get('runtime') else not diagnostic,(case['name'],diagnostic)
    rows=browser.js("return Array.from(document.querySelectorAll('#output-file-list li span')).map(n=>n.textContent);")
    assert rows==[f"{f['name']} ({len(contents(f))} bytes)" for f in case['resultFiles']],(case['name'],rows)
    assert browser.js('return document.querySelectorAll("img").length;')==0
    for index,file in enumerate(case['resultFiles']):
        target=browser.downloads/file.get('downloadName',file['name']);target.unlink(missing_ok=True)
        element=browser.command('POST','/element',{'using':'css selector','value':f'#output-file-list li:nth-child({index+1}) button'})
        reference=element['element-6066-11e4-a52e-4f735466cecf']
        browser.command('POST',f'/element/{reference}/click',{})
        expected=contents(file);deadline=time.monotonic()+15
        while time.monotonic()<deadline:
            if target.exists() and target.read_bytes()==expected:
                break
            time.sleep(.05)
        else:
            raise AssertionError(f'{browser.kind}: missing/changed download {target.name}; saved={[p.name for p in browser.downloads.iterdir()]}')
    if case['name']=='rewrite_input' and mode=='batch':
        kind='offline' if browser.js('return location.protocol;')=='file:' else 'playground'
        Path(f'build/files-{browser.kind}-{kind}.png').write_bytes(base64.b64decode(browser.command('GET','/screenshot')))


def verify_pending(browser):
    """Run stays disabled during File reads; Reset cannot revive stale selections."""
    browser.page_async('''const original=Blob.prototype.arrayBuffer;window.restoreFileRead=()=>{Blob.prototype.arrayBuffer=original;};
        Blob.prototype.arrayBuffer=function(){if(this.name==='slow.txt')return new Promise(resolve=>window.releaseFileRead=()=>resolve(new Uint8Array([97,98,99]).buffer));return original.call(this);};
        const transfer=new DataTransfer();transfer.items.add(new File(['abc'],'slow.txt'));
        const picker=document.getElementById('input-files');picker.files=transfer.files;picker.dispatchEvent(new Event('change'));
        arguments[arguments.length-1](true);''')
    try:
        assert browser.js("return document.getElementById('run').disabled;")
        reset=browser.js("return !!document.getElementById('reset');")
        if reset:
            browser.click('reset')
        browser.page_async('window.releaseFileRead();window.restoreFileRead();arguments[arguments.length-1](true);')
        browser.wait("!document.getElementById('run').disabled")
        browser.wait(f"document.querySelectorAll('#input-file-list li').length==={0 if reset else 1}")
        if not reset:
            browser.click('clear-files')
    finally:
        browser.page_async('window.restoreFileRead();delete window.restoreFileRead;delete window.releaseFileRead;arguments[arguments.length-1](true);')


def verify_ui(browser, directory):
    """Real uploads/downloads in both modes, including immutable rerun inputs."""
    verify_pending(browser)
    verify_export_privacy(browser,directory)
    for case in fixtures():
        browser.js("const source=document.getElementById('source');source.value=arguments[0];source.dispatchEvent(new Event('input'));",case['source'])
        select_files(browser,case['files'],directory/'selected-files')
        for mode in ('batch','interactive'):
            verify_case(browser,case,mode)
        if case['name']=='rewrite_input':
            verify_case(browser,case,'batch')
        if case['name']=='live_copy':
            interactive.select_mode(browser,'interactive');browser.click('run')
            browser.wait("document.getElementById('status').textContent==='Waiting for input'")
            assert browser.js("return document.getElementById('input-files').disabled;")
            assert browser.js("return document.querySelectorAll('#output-file-list li').length;")==0
            browser.click('stop')
            assert browser.js("return document.querySelectorAll('#output-file-list li').length;")==0
            verify_case(browser,case,'interactive')
    # Source/compile failure must invalidate a previous successful result immediately.
    browser.js("const source=document.getElementById('source');source.value='program p; begin writeln(missing) end.';source.dispatchEvent(new Event('input'));")
    assert browser.js("return document.querySelectorAll('#output-file-list li').length;")==0
    browser.click('run');browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'E_UNKNOWN' in browser.js("return document.getElementById('diagnostics').textContent;")
    browser.js("const source=document.getElementById('source');source.value=\"program p; var f:text; begin assign(f,'partial');rewrite(f);write(f,'x');while true do begin end end.\";source.dispatchEvent(new Event('input'));")
    browser.click('run');browser.wait("document.getElementById('status').textContent==='Failed'")
    assert 'R_TIMEOUT' in browser.js("return document.getElementById('diagnostics').textContent;")
    assert browser.js("return document.querySelectorAll('#output-file-list li').length;")==0
    if not browser.js("return document.getElementById('clear-files').disabled;"):
        browser.click('clear-files')


def verify_export_privacy(browser, directory):
    """The real HTML download must not embed selected inputs or console output."""
    secret='private-input-9e72a45d-should-not-be-exported'
    case={**next(c for c in fixtures() if c['name']=='read_input'),
          'files':[dict(name='input.txt',text=secret+'\n')], 'output':secret+'\n'}
    browser.js("const s=document.getElementById('source');s.value=arguments[0];s.dispatchEvent(new Event('input'));",case['source'])
    select_files(browser,case['files'],directory/'privacy-selected-files')
    verify_case(browser,case,'batch')
    target=browser.downloads/'program.html';target.unlink(missing_ok=True);browser.click('download-html')
    deadline=time.monotonic()+15
    while time.monotonic()<deadline:
        if target.exists() and target.read_bytes().rstrip().endswith(b'</html>'):
            html=target.read_bytes();break
        time.sleep(.05)
    else:
        raise AssertionError('HTML export download did not complete')
    assert secret.encode() not in html and base64.b64encode(secret.encode()) not in html
    target.replace(directory/'input-free-export.html')
