# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Independent public-asset embedding checks and reproducible browser measurements.

No browser/Python packages. Runs standalone or inside the full browser acceptance gate.
Only the documented distribution is copied into a nested static hosting directory.
"""
import argparse
import functools
import gzip
import http.server
import json
from pathlib import Path
import re
import shutil
import tempfile
import threading


class EmbeddingHandler(http.server.SimpleHTTPRequestHandler):
    """Record network traffic to verify that only static GETs reach this server."""
    requests = []

    def log_message(self, format, *args):
        """Suppress routine successful and intentionally missing asset requests."""

    def do_GET(self):
        """Record asset requests without recording source or program input."""
        self.requests.append(('GET', self.path))
        try:
            super().do_GET()
        except (BrokenPipeError, ConnectionResetError):
            pass  # Expected when initialization cancels parallel asset fetches.

    def do_POST(self):
        """Reject and expose an unexpected network transmission."""
        self.requests.append(('POST', self.path))
        self.send_error(405)


def verify_browser(browser, directory):
    """Exercise the distributed API/example/snippets from a nested site directory."""
    root = directory / 'embedding-site'
    assets = root / 'deep' / 'pas2wasm'
    (assets / 'examples').mkdir(parents=True)
    names = ['engine.js', 'pas2wasm.js', 'pas2wasm.wasm', 'compiler-worker.js',
             'LICENSE', 'THIRD_PARTY_NOTICES.txt']
    for name in names:
        shutil.copyfile(Path('build') / name, assets / name)
    (assets / 'checks.html').write_text('<!doctype html><title>Headless API checks</title><script src="engine.js"></script>')
    shutil.copyfile('build/examples/embedding.html', assets / 'examples' / 'embedding.html')
    handler = functools.partial(EmbeddingHandler, directory=str(root))
    server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    base = f'http://127.0.0.1:{server.server_port}/deep/pas2wasm/'
    start_requests = len(EmbeddingHandler.requests)
    try:
        # First compiler initialization uses a fresh profile and uncached asset URLs.
        browser.navigate(base + 'checks.html')
        fixtures = json.loads(Path('test/file-transfer.json').read_text())
        fixtures = [case for case in fixtures if 'source' in case and case['name'] != 'live_copy']
        cases = json.loads(Path('test/cases.json').read_text())
        programs = [case for case in cases if case['name'] in
                    ['tutorial_hello', 'tutorial_formatted_io', 'tutorial_pointer_demo2_unchanged', 'tutorial_textfiles']]
        checks = Path('test/engine.js').read_text()
        result = browser.page_async(checks + '''const done=arguments[arguments.length-1];
            verifyPas2WasmEngine(...Array.from(arguments).slice(0,-1))
                .then(done,error=>done({error:error.stack}));''',
            base, Path('VERSION').read_text().strip(), fixtures, programs)
        assert 'error' not in result, result
        result['assets'] = {name: {'bytes': (assets / name).stat().st_size,
                                 'gzipBytes': len(gzip.compress((assets / name).read_bytes(), mtime=0))}
                            for name in names if name.endswith(('.js', '.wasm'))}
        Path('build', f'embedding-{browser.kind}.json').write_text(json.dumps(result, indent=2) + '\n')

        browser.navigate(base + 'examples/embedding.html')
        browser.wait("document.getElementById('progress').textContent.startsWith('Ready')")
        assert browser.js("return document.getElementById('source')===null && document.getElementById('terminal')===null;")
        browser.click('execute')
        browser.wait("document.getElementById('progress').textContent==='completed'")
        assert browser.js("return document.getElementById('transcript').textContent;") == 'Hello, world\n'
        browser.js("document.getElementById('pascal').value='program p;var n:integer;begin readln(n);writeln(n*2)end.';document.getElementById('stdin').value='21';")
        browser.click('execute')
        browser.wait("document.getElementById('progress').textContent==='completed'")
        assert browser.js("return document.getElementById('transcript').textContent;") == '42\n'
        browser.js("document.getElementById('pascal').value='program p;begin while true do begin end end.';")
        browser.click('execute')
        browser.wait("document.getElementById('progress').textContent==='running'")
        browser.click('cancel')
        browser.wait("document.getElementById('progress').textContent==='stopped'")

        # Both quick starts are complete HTML, run unchanged under their documented
        # relative asset layout. A missing snippet is a failure, not a skipped test.
        for document in ['README.md', 'docs/browser-embedding.md']:
            text = Path(document).read_text()
            snippet = re.search(r'```html\n(<!doctype html>.*?)\n```', text, re.S)
            assert snippet, f'{document}: complete embedding quick start missing'
            page = root / 'deep' / (Path(document).stem.lower() + '.html')
            page.write_text(snippet[1])
            browser.navigate(base + '../' + page.name)
            browser.wait("document.getElementById('result').textContent==='Hello, world\\n'")
        assert all(method == 'GET' for method, path in EmbeddingHandler.requests[start_requests:]), 'Unexpected source/input network transmission'
        print(f'{browser.kind}: {result["checks"]} public engine assertions, nested asset layout, '
              'independent example and README/guide quick starts passed; '
              f'benchmark: build/embedding-{browser.kind}.json', flush=True)
    finally:
        server.shutdown()
        server.server_close()


def main():
    """Run only embedding acceptance; the full gate calls the same checks."""
    from browser import Browser
    parser = argparse.ArgumentParser()
    parser.add_argument('--browser', choices=['chromium', 'firefox', 'both'], default='both')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='pas2wasm-embedding-') as temporary:
        for kind in (['chromium', 'firefox'] if args.browser == 'both' else [args.browser]):
            directory = Path(temporary) / kind
            directory.mkdir()
            browser = Browser(kind, directory)
            try:
                verify_browser(browser, directory)
            finally:
                browser.close()


if __name__ == '__main__':
    main()
