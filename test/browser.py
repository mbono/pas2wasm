# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Real Chromium/Firefox acceptance via standard WebDriver; no Python packages.

Driver processes and profiles are temporary. Browsers retain default web security.
Run from the repository root after make; use --browser chromium for a quick pass.
"""
import argparse
import interactive
import terminal
import base64
import functools
import http.server
import json
import re
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import threading
import time
import urllib.error
import urllib.request


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    """Serve the release directory and record requests for the offline audit."""
    requests = []

    def log_message(self, format, *args):
        """Suppress routine server noise; failures remain visible in assertions."""

    def do_GET(self):
        """Record each requested path before serving it."""
        self.requests.append(self.path)
        super().do_GET()


def free_port():
    """Reserve an ephemeral local port number for a driver process."""
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        return sock.getsockname()[1]


class Browser:
    """Own a temporary WebDriver process/session and its headless browser."""
    def __init__(self, kind, directory):
        """Start the selected driver with an isolated browser profile."""
        self.kind = kind
        self.session = None
        self.downloads = directory / 'downloads'
        self.downloads.mkdir()
        port = free_port()
        self.base = f'http://127.0.0.1:{port}'
        executable = 'chromedriver' if kind == 'chromium' else 'geckodriver'
        if not shutil.which(executable):
            raise RuntimeError(f'{executable} is required for make test-browser')
        self.log = open(directory / f'{kind}-driver.log', 'w')
        driver_args = [executable, f'--port={port}']
        if kind == 'firefox':
            driver_args.append('--allow-system-access')
        self.process = subprocess.Popen(driver_args, stdout=self.log, stderr=self.log)
        try:
            for _ in range(150):
                try:
                    self.request('GET', '/status')
                    break
                except (OSError, urllib.error.URLError):
                    if self.process.poll() is not None:
                        raise RuntimeError(f'{executable} stopped; see {self.log.name}')
                    time.sleep(.1)
            options = {'browserName': 'chrome' if kind == 'chromium' else 'firefox'}
            if kind == 'chromium':
                options['goog:chromeOptions'] = {'prefs': {
                    'download.default_directory':str(self.downloads),
                    'download.prompt_for_download':False}, 'args': [
                    '--headless=new', '--disable-dev-shm-usage', '--no-first-run',
                    '--no-default-browser-check', '--disable-background-networking',
                    f'--user-data-dir={directory / "chrome-profile"}'
                ]}
            else:
                options['moz:firefoxOptions'] = {'args': ['-headless'], 'prefs': {
                    'browser.download.dir':str(self.downloads), 'browser.download.folderList':2,
                    'browser.download.useDownloadDir':True,
                    'browser.helperApps.neverAsk.saveToDisk':'application/wasm,text/plain,text/html',
                    'browser.download.alwaysOpenPanel':False}}
            self.session = self.request('POST', '/session', {'capabilities': {'alwaysMatch': options}})['sessionId']
            self.command('POST', '/timeouts', {'script': 120000, 'pageLoad': 30000, 'implicit': 0})
            self.command('POST', '/window/rect', {'width':1280, 'height':1000})
        except BaseException:
            self.close()
            raise

    def request(self, method, path, data=None):
        """Send one JSON WebDriver request, preserving protocol error messages."""
        encoded = json.dumps(data).encode() if data is not None else None
        request = urllib.request.Request(self.base + path, data=encoded, method=method,
                                         headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=150) as response:
                result = json.load(response).get('value')
        except urllib.error.HTTPError as error:
            raise RuntimeError(error.read().decode()) from error
        return result

    def command(self, method, path, data=None):
        """Address a command to the owned browser session."""
        return self.request(method, f'/session/{self.session}{path}', data)

    def js(self, script, *args, asynchronous=False):
        """Evaluate test code in the active page; async code must call its callback."""
        return self.command('POST', '/execute/async' if asynchronous else '/execute/sync',
                            {'script': script, 'args': args})

    def navigate(self, url):
        """Navigate with ordinary browser security settings."""
        self.command('POST', '/url', {'url': url})

    def click(self, element_id):
        """Use a trusted WebDriver click, needed for browser download activation."""
        element = self.command('POST','/element',{'using':'css selector','value':'#'+element_id})
        reference = element['element-6066-11e4-a52e-4f735466cecf']
        self.command('POST',f'/element/{reference}/click',{})

    def page_async(self, script, *args):
        """Run Wasm host functions in the page realm, avoiding Firefox's WebDriver
        sandbox wrappers, which are not callable Wasm imports. ASCII JSON transports
        intentional invalid-Unicode fixtures; the page reconstructs them. Results use text."""
        self.js('''const previous=document.getElementById('webdriver-result');
            if(previous) previous.remove();
            const result=document.createElement('pre');result.id='webdriver-result';result.hidden=true;
            document.body.appendChild(result);
            const script=document.createElement('script');
            script.textContent='(function(){'+arguments[0]+'}).apply(null,'+
            arguments[1]+'.concat(function(value){document.getElementById("webdriver-result").textContent=JSON.stringify(value);}));';
        document.body.appendChild(script);script.remove();''', script, json.dumps(list(args)))
        self.wait("document.getElementById('webdriver-result').textContent", timeout=120)
        return self.js("return JSON.parse(document.getElementById('webdriver-result').textContent);")

    def wait(self, predicate, timeout=15):
        """Poll a concrete page condition with a bounded deadline."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if value := self.js(f'return ({predicate});'):
                return value
            time.sleep(.05)
        raise AssertionError(f'{self.kind}: timed out waiting for {predicate}; '
                             f'page={self.js("return document.body.innerText")[:1500]}')

    def offline(self):
        """Disable networking through the driver's supported browser controls."""
        if self.kind == 'chromium':
            self.command('POST', '/goog/cdp/execute', {'cmd':'Network.enable', 'params':{}})
            self.command('POST', '/goog/cdp/execute', {'cmd':'Network.emulateNetworkConditions',
                'params':{'offline':True, 'latency':0, 'downloadThroughput':0, 'uploadThroughput':0}})
        else:
            self.command('POST', '/moz/context', {'context':'chrome'})
            try:
                self.js('Services.io.manageOfflineStatus = false; Services.io.offline = true;')
            finally:
                self.command('POST', '/moz/context', {'context':'content'})

    def close(self):
        """Stop the browser and driver even after a failed test."""
        if self.session:
            try:
                self.command('DELETE', '')
            except Exception:
                pass
            self.session = None
        self.process.terminate()
        try:
            self.process.wait(timeout=10)
        except subprocess.TimeoutExpired:
            self.process.kill()
        self.log.close()


def native_result(source, directory, name):
    """Produce authoritative native bytes or a structured diagnostic for parity."""
    input_path, output_path = directory / f'{name}.pas', directory / f'{name}.wasm'
    input_path.write_text(source)
    result = subprocess.run(['./build/pas2wasm', '--diagnostics-json', '--emit-wat',
                             '-o', str(output_path), str(input_path)], capture_output=True, text=True, check=False)
    if result.returncode:
        return {'diagnostic':json.loads(result.stderr)}
    return {'binary':list(output_path.read_bytes()), 'wat':output_path.with_suffix('.wat').read_text()}


def run_ui(browser, source, input_text='', wat=False):
    """Use production UI controls; source edits trigger ordinary invalidation."""
    browser.js('''const mode=document.getElementById('io-mode');
        if(mode.value!=='batch'){mode.value='batch';mode.dispatchEvent(new Event('change'));}
        const source = document.getElementById('source');
        source.value=arguments[0]; source.dispatchEvent(new Event('input'));
        document.getElementById('input').value=arguments[1];
        const option=document.getElementById('generate-wat'); option.checked=arguments[2];
        option.dispatchEvent(new Event('change')); document.getElementById('run').click();''', source, input_text, wat)
    browser.wait("['Completed','Failed'].includes(document.getElementById('status').textContent)")
    return browser.js("return {output:document.getElementById('output').textContent, diagnostics:document.getElementById('diagnostics').textContent};")


def verify_heap_boundaries(browser, cases, directory):
    """Test high generations/borrows in the actual engine with test-only WAT exports."""
    source = next(t['source'] for t in cases if t['name']=='pointer_empty_heap')
    artifact = native_result(source, directory, 'private-heap')
    exports = '\n'.join(f'(export "{name}" (func $p2w_{name}))'
                        for name in ['allocate','validate','borrow','release','unpin'])
    wat = re.sub(r'\)\s*$', exports+'\n)', artifact['wat'])
    input_path, output_path = directory/'private-heap.wat', directory/'private-heap.wasm'
    input_path.write_text(wat)
    subprocess.run(['wasm-as',str(input_path),'-o',str(output_path)],check=True,capture_output=True)
    result = browser.page_async('''const binary=Uint8Array.from(arguments[0]),done=arguments[arguments.length-1];
        (async()=>{
            const host=createPas2WasmRuntime('',()=>{});
            const {instance}=await WebAssembly.instantiate(binary,{pas2wasm_v1:host.imports});
            host.attach(instance); const h=instance.exports;
            const first=h.allocate(16,1,2), address=Number(BigInt.asUintN(32,first));
            h.release(first,16,2,2);
            new DataView(h.memory.buffer).setUint32(address-24,0xfffffffe,true);
            const last=h.allocate(16,3,2);
            if((BigInt.asUintN(64,last)>>32n)!==0xffffffffn) throw Error('generation bits');
            if(h.validate(last,16,4,2,0)!==address) throw Error('validation');
            let stale,lifetime,wrap;
            try {h.validate(first,16,5,2,0);} catch(e){stale=e.code;}
            h.borrow(last,16,6,2);
            try {h.release(last,16,7,2);} catch(e){lifetime=e.code;}
            h.unpin(0);h.release(last,16,8,2);
            try {h.allocate(16,9,2);} catch(e){wrap=e.code;}
            done({stale,lifetime,wrap});
        })().catch(e=>done({error:String(e)}));''', list(output_path.read_bytes()))
    assert result == {'stale':'R_DANGLING','lifetime':'R_LIFETIME','wrap':'R_MEMORY'},result


def verify_browser(kind, base_url, cases, directory):
    """Check worker parity, UI state, offline reruns, and file:// HTML exports."""
    project_license = Path('LICENSE').read_text()
    assert Path('build/LICENSE').read_text() == project_license
    assert Path('build/THIRD_PARTY_NOTICES.txt').read_text() == Path('docs/third-party-notices.txt').read_text()
    browser = Browser(kind, directory)
    try:
        browser.navigate(base_url)
        browser.wait("document.getElementById('status').textContent === 'Ready'")
        assert browser.js("return document.getElementById('io-mode').value;") == 'interactive'
        assert browser.js("return document.getElementById('batch-input').hidden;")
        browser.click('run')
        browser.wait("document.getElementById('status').textContent==='Waiting for input'")
        interactive.submit(browser,'6',keyboard=True)
        browser.wait("document.getElementById('status').textContent==='Completed'")
        assert browser.js("return document.getElementById('output').textContent;") == '6\nTwice your number: 12\n'
        version = Path('VERSION').read_text(encoding='utf-8').strip()
        assert browser.js("return document.querySelector('h1').textContent;") == f'Pas2Wasm (v{version})'
        verify_heap_boundaries(browser,cases,directory)
        legacy = directory/'legacy-io.wasm'
        subprocess.run(['wasm-as','test/fixtures/legacy-io.wat','-o',str(legacy)],check=True,capture_output=True)
        live_examples = interactive.verify_workers(browser, lambda source,name: native_result(source,directory,name),
                                                   {'binary':list(legacy.read_bytes())})
        interactive.verify_clock(browser)
        interactive.verify_ui(browser)
        terminal.verify_ui(browser)
        # Exercise the exact bundled worker implementation. Do not inspect app
        # lexical variables: Firefox WebDriver evaluates in a separate sandbox.
        results = browser.page_async('''const cases=arguments[0], done=arguments[arguments.length-1];
            (async () => {
                const [loader,compilerBytes,bootstrap]=await Promise.all([
                    fetch('pas2wasm.js').then(r=>r.text()),
                    fetch('pas2wasm.wasm').then(r=>r.arrayBuffer()),
                    fetch('compiler-worker.js').then(r=>r.text())
                ]);
                const script=[loader,createPas2WasmCompiler.toString(),bootstrap].join(String.fromCharCode(10));
                const url=URL.createObjectURL(new Blob([script],{type:'text/javascript'}));
                const worker=new Worker(url); URL.revokeObjectURL(url);
                const request = data => new Promise((resolve,reject) => {
                    worker.onmessage=event=>resolve(event.data); worker.onerror=event=>reject(new Error(event.message));
                    worker.postMessage(data);
                });
                try {
                    const ready = await request({type:'init',binary:compilerBytes});
                    const results=[];
                    for (let i=0;i<cases.length;i++) {
                        const {result}=await request({id:i,source:cases[i].source,emitWat:true});
                        if(result.diagnostic) {results.push({diagnostic:result.diagnostic});continue;}
                        let output='', diagnostic=null;
                        const host=createPas2WasmRuntime(cases[i].input||'',text=>output+=text,
                            {randomSeed:cases[i].randomSeed});
                        const {instance}=await WebAssembly.instantiate(result.binary,{pas2wasm_v1:host.imports});
                        host.attach(instance);
                        try{instance.exports.run();}catch(error){diagnostic={code:error.code,line:error.line,column:error.column};}
                        results.push({binary:Array.from(result.binary),wat:result.wat,
                            output:cases[i].runtime?null:output,runtime:diagnostic});
                    }
                    done({version:ready.version, results});
                } finally {worker.terminate();}
            })().catch(error=>done({error:error.name+': '+error.message+' | '+error.stack}));''', cases)
        assert not results.get('error'), results
        assert results['version'] == version, (kind, 'worker compiler version', results['version'])
        for test, result in zip(cases, results['results'], strict=True):
            expected = native_result(test['source'], directory, test['name'])
            if 'diagnostic' in expected:
                assert result == expected, (kind, test['name'], result, expected)
            else:
                assert 'binary' in result, (kind, test['name'], result)
                assert result['binary'] == expected['binary'], (kind, test['name'], 'binary parity')
                assert result['wat'] == expected['wat'], (kind, test['name'], 'WAT parity')
                if test.get('runtime'):
                    assert result['runtime']['code'] == test['runtime'], (kind, test['name'], result)
                    for expected_key, actual_key in [('runtimeLine','line'),('runtimeColumn','column')]:
                        if expected_key in test:
                            assert result['runtime'][actual_key] == test[expected_key], (kind, test['name'], result)
                else:
                    assert result['output'] == test['output'], (kind, test['name'], result)
        examples = {name:next(test for test in cases if test['name']==case_name)
                    for name, case_name in {
                        'identifiers':'identifier_underscore_declarations', 'streamio':'io_same_line',
                        'increment':'tutorial_tabellina',
                        'declarationorder':'tutorial_parametri',
                        'declarationcalls':'tutorial_subroutine',
                        'booleanorder':'boolean_order_runtime',
                        'mixedtext':'tutorial_if_else',
                        'formatting':'tutorial_formatted_io', 'formatio':'tutorial_io',
                        'randomvalues':'random_range_checks',
                        'matrices':'matrix_range_checks',
                        'numericcharacters':'numeric_character_unicode',
                        'subranges':'tutorial_subrange', 'subrangevalues':'subrange_aggregate_heap',
                        'loops':'for_mixed_loops', 'routines':'procedure_recursion',
                        'functions':'function_factorial', 'exit':'exit_recursive_function',
                        'case':'case_exit_function', 'constants':'constant_scopes',
                        'aliases':'alias_identity', 'ranges':'case_named_ranges',
                        'valueexit':'exit_value_recursive', 'forwards':'forward_mutual_functions',
                        'mutualprocedures':'forward_mutual_procedures',
                        'frames':'frame_repeated_early_returns', 'arrays':'array_bounds_and_copy',
                        'records':'record_recursive_result', 'aggregate':'aggregate_loop_temporary_reuse',
                        'references':'reference_exit_cleanup', 'referencevalues':'reference_fields_elements',
                        'nested':'nested_recursive_activations', 'nestedvalues':'nested_early_result_cleanup',
                        'reals':'real_nested_values', 'realzero':'real_signed_zero',
                        'strings':'string_nested_values', 'stringstorage':'string_record_copy',
                        'stringio':'string_whole_line', 'stringhtml':'string_html_text',
                        'characters':'character_reference_alias_width', 'slices':'character_slice',
                        'characterio':'character_input', 'charactercaptures':'character_reference_nested_capture',
                        'pointers':'pointer_recursive_list', 'pointervalues':'pointer_all_value_types',
                        'pointerresults':'pointer_returned_destinations', 'pointerreuse':'pointer_repeated_reuse',
                        'pointercaptures':'pointer_captured_heap_reference'
                    }.items()}
        for example in examples.values():
            assert run_ui(browser, example['source'], example.get('input',''), wat=True) == {
                'output':example['output'], 'diagnostics':''}
        double = next(test for test in cases if test['name']=='double')
        result = run_ui(browser, double['source'], '21', True)
        assert result == {'output':'42\n','diagnostics':''}, (kind, result)
        assert browser.js("return document.getElementById('wat-view').textContent.startsWith('(module');")
        # Exercise actual download controls and inspect saved artifact contents.
        expected = native_result(double['source'],directory,'download')
        for extension in ['wasm','wat','html']:
            browser.click('download-'+extension)
            artifact = browser.downloads / f'program.{extension}'
            deadline = time.monotonic() + 10
            while not artifact.exists() and time.monotonic() < deadline:
                time.sleep(.05)
            assert artifact.exists(), (kind, 'download missing', extension,
                                       [path.name for path in browser.downloads.iterdir()])
            if extension=='wasm':
                assert list(artifact.read_bytes())==expected['binary']
            elif extension=='wat':
                assert artifact.read_text()==expected['wat']
            else:
                assert 'exportedPas2WasmRunner(' in artifact.read_text()
        # Stop must work during compilation as well as execution.
        browser.js("document.getElementById('run').click();document.getElementById('stop').click();")
        assert browser.js("return document.getElementById('status').textContent==='Stopped' && document.getElementById('download-html').disabled;")
        assert run_ui(browser,double['source'],'21',True)['output']=='42\n'
        browser.js("document.getElementById('generate-wat').checked=false; document.getElementById('generate-wat').dispatchEvent(new Event('change'));")
        assert browser.js("return document.getElementById('download-wat').disabled && !document.getElementById('wat-view').textContent && !document.getElementById('download-html').disabled;")
        for _ in range(2):
            assert run_ui(browser,double['source'],'21')['output']=='42\n'
        assert 'E_UNKNOWN' in run_ui(browser,'program bad; begin writeln(missing) end.')['diagnostics']
        assert browser.js("return ['html','wasm','wat'].every(type=>document.getElementById('download-'+type).disabled);")
        assert browser.js("return document.getElementById('output').textContent === '';")
        infinite = 'program forever; begin while true do begin end end.'
        browser.js("const s=document.getElementById('source');s.value=arguments[0];s.dispatchEvent(new Event('input'));document.getElementById('run').click();",infinite)
        browser.wait("document.getElementById('status').textContent === 'Running…'")
        browser.js("document.getElementById('stop').click();")
        assert browser.js("return document.getElementById('status').textContent === 'Stopped';")
        assert run_ui(browser,double['source'],'21')['output']=='42\n'
        assert 'R_TIMEOUT' in run_ui(browser,infinite)['diagnostics']
        assert run_ui(browser,double['source'],'21')['output']=='42\n'
        browser.js("document.getElementById('reset').click();")
        assert browser.js("return document.getElementById('source').value.includes('program double') && document.getElementById('input').value==='21' && document.getElementById('output').textContent==='' && document.getElementById('diagnostics').textContent==='';")
        # Record browser rendering for visual review.
        screenshot = browser.command('GET','/screenshot')
        Path('build',f'{kind}-playground.png').write_bytes(base64.b64decode(screenshot))
        # Block all networking after initial load; re-created compiler workers still work.
        browser.offline()
        request_count = len(QuietHandler.requests)
        assert run_ui(browser,double['source'],'21',True)['output']=='42\n'
        assert browser.js("return document.getElementById('wat-view').textContent.startsWith('(module');")
        assert len(QuietHandler.requests)==request_count, 'Unexpected offline request'
        exports = []
        export_sources = {name:example['source'] for name, example in examples.items()}
        export_sources.update({
            'double':double['source'],
            'literal':next(t['source'] for t in cases if t['name']=='utf8_quotes'),
            'result_error':next(t['source'] for t in cases if t['name']=='function_missing_result'),
            'frame_error':next(t['source'] for t in cases if t['name']=='frame_budget_exhaustion'),
            'bounds_error':next(t['source'] for t in cases if t['name']=='array_lower_bound'),
            'control_error':next(t['source'] for t in cases if t['name']=='reference_hidden_control'),
            'real_error':next(t['source'] for t in cases if t['name']=='real_overflow'),
            'subrange_error':next(t['source'] for t in cases if t['name']=='subrange_assignment_range'),
            'string_error':next(t['source'] for t in cases if t['name']=='string_capacity_location'),
            'string_memory':next(t['source'] for t in cases if t['name']=='string_frame_budget'),
            'character_error':next(t['source'] for t in cases if t['name']=='character_chr_location'),
            'character_bounds':next(t['source'] for t in cases if t['name']=='character_reference_invalid_read'),
            'character_memory':next(t['source'] for t in cases if t['name']=='character_slice_frame_exhaustion'),
            'pointer_nil':next(t['source'] for t in cases if t['name']=='pointer_nil_location'),
            'pointer_dangling':next(t['source'] for t in cases if t['name']=='pointer_dangling_after_reuse'),
            'pointer_release':next(t['source'] for t in cases if t['name']=='pointer_alias_dispose'),
            'pointer_lifetime':next(t['source'] for t in cases if t['name']=='pointer_character_view_lifetime'),
            'pointer_memory':next(t['source'] for t in cases if t['name']=='pointer_allocation_exhaustion'),
            'pointer_borrow_memory':next(t['source'] for t in cases if t['name']=='pointer_borrow_budget_exhaustion'),
            'infinite':infinite
        })
        export_sources['terminal_editor'] = terminal.source()
        export_sources['echo_limit'] = interactive.limit_source()
        export_sources.update({'live_'+case['name']:case['source'] for case in live_examples if case['name']!='invalid_unicode'})
        for name, source in export_sources.items():
            artifact = native_result(source,directory,f'export-{name}')
            html = browser.js('return exportPas2WasmHtml(Uint8Array.from(arguments[0]));',artifact['binary'])
            assert '<!--\n' + project_license + '-->' in html, (kind, name, 'MIT notice')
            target = directory / f'{kind}-{name}.html'
            target.write_text(html)
            exports.append((name,target))
        # Use an entirely fresh profile, with networking disabled before opening
        # relocated files. This rules out a dependency on playground caches.
        browser.close()
        local_directory = directory / 'fresh-local-profile'
        local_directory.mkdir()
        browser = Browser(kind, local_directory)
        browser.navigate('about:blank')
        browser.offline()
        relocated = directory / 'relocated programs'
        relocated.mkdir()
        for name, target in exports:
            target = Path(shutil.copy2(target, relocated / target.name))
            browser.navigate(target.as_uri())
            assert browser.js("return document.getElementById('input').value === '' && document.getElementById('status').textContent === 'Ready';")
            assert browser.js("return document.getElementById('io-mode').value;") == 'interactive'
            if not (name.startswith('live_') or name in ('terminal_editor','echo_limit')):
                interactive.select_mode(browser,'batch')
            if name.startswith('live_'):
                interactive.select_mode(browser,'interactive')
                case=next(c for c in live_examples if name=='live_'+c['name'])
                for _ in range(2):
                    interactive.verify_console_case(browser,case)
                if name=='live_prompts':
                    interactive.verify_offline_controls(browser,case)
            elif name=='terminal_editor':
                terminal.verify(browser)
            elif name=='echo_limit':
                interactive.verify_input_limit(browser)
            elif name=='double':
                for _ in range(2):
                    browser.js("document.getElementById('input').value='21';document.getElementById('run').click();")
                    browser.wait("document.getElementById('status').textContent==='Completed'")
                    assert browser.js("return document.getElementById('output').textContent;")=='42\n'
                browser.js("document.getElementById('input').value='12abc';document.getElementById('run').click();")
                browser.wait("document.getElementById('status').textContent==='Failed'")
                assert 'R_INPUT_INVALID' in browser.js("return document.getElementById('diagnostics').textContent;")
            elif name=='literal':
                browser.js("document.getElementById('run').click();")
                browser.wait("document.getElementById('status').textContent==='Completed'")
                assert browser.js("return document.getElementById('output').textContent;")==next(t['output'] for t in cases if t['name']=='utf8_quotes')
                assert browser.js("return document.querySelectorAll('img').length;")==0
            elif name in examples:
                for _ in range(2):
                    browser.js("document.getElementById('input').value=arguments[0];document.getElementById('run').click();",examples[name].get('input',''))
                    browser.wait("document.getElementById('status').textContent==='Completed'")
                    assert browser.js("return document.getElementById('output').textContent;")==examples[name]['output']
                    assert browser.js("return document.querySelectorAll('img').length;")==0
            elif name in ('frame_error','bounds_error','control_error','real_error','subrange_error','string_error','string_memory','character_error','character_bounds','character_memory','pointer_nil','pointer_dangling','pointer_release','pointer_lifetime','pointer_memory','pointer_borrow_memory'):
                for _ in range(2):
                    browser.js("document.getElementById('run').click();")
                    browser.wait("document.getElementById('status').textContent==='Failed'")
                    assert ({'frame_error':'R_MEMORY','bounds_error':'R_BOUNDS','control_error':'R_FOR_CONTROL','real_error':'R_REAL_RANGE','subrange_error':'R_RANGE','string_error':'R_STRING_CAPACITY','string_memory':'R_MEMORY','character_error':'R_CHARACTER','character_bounds':'R_BOUNDS','character_memory':'R_MEMORY','pointer_nil':'R_NIL','pointer_dangling':'R_DANGLING','pointer_release':'R_DISPOSE','pointer_lifetime':'R_LIFETIME','pointer_memory':'R_MEMORY','pointer_borrow_memory':'R_MEMORY'}[name]) in browser.js("return document.getElementById('diagnostics').textContent;")
            elif name=='result_error':
                browser.js("document.getElementById('run').click();")
                browser.wait("document.getElementById('status').textContent==='Failed'")
                assert 'R_RESULT' in browser.js("return document.getElementById('diagnostics').textContent;")
            else:
                browser.js("document.getElementById('run').click();")
                browser.wait("document.getElementById('status').textContent==='Running…'")
                browser.js("document.getElementById('stop').click();")
                assert browser.js("return document.getElementById('status').textContent;")=='Stopped'
                browser.js("document.getElementById('run').click();")
                browser.wait("document.getElementById('status').textContent==='Failed'")
                assert 'R_TIMEOUT' in browser.js("return document.getElementById('diagnostics').textContent;")
        print(f'{kind}: {len(cases)} browser-worker parity cases; UI, JSPI live/batch parity, cumulative clocks, Stop/timeout, offline compile/WAT, file:// exports passed.',flush=True)
    finally:
        browser.close()


def main():
    """Serve a built release and run both mandated browser suites."""
    parser = argparse.ArgumentParser()
    parser.add_argument('--browser',choices=['chromium','firefox','both'],default='both')
    args = parser.parse_args()
    cases = json.loads(Path(__file__).with_name('cases.json').read_text(encoding='utf-8'))
    handler = functools.partial(QuietHandler, directory='build')
    server = http.server.ThreadingHTTPServer(('127.0.0.1',0),handler)
    thread = threading.Thread(target=server.serve_forever,daemon=True)
    thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='pas2wasm-browsers-') as temporary:
            for kind in (['chromium','firefox'] if args.browser=='both' else [args.browser]):
                directory = Path(temporary)/kind
                directory.mkdir()
                verify_browser(kind,f'http://127.0.0.1:{server.server_port}/',cases,directory)
    finally:
        server.shutdown()
        server.server_close()


if __name__=='__main__':
    main()
