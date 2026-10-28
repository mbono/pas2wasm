# Browser embedding

Pas2Wasm's headless JavaScript engine compiles Pascal using the Wasm build of the C
compiler and runs generated programs in separate terminable workers. The host page
provides callbacks for output, input requests and states. It needs no playground markup,
package manager or compilation server.

## Static files and quick start

Run `make site`. Copy this matching release set from `build/`:

```text
my-site/
  index.html
  pas2wasm/
    engine.js
    pas2wasm.js
    pas2wasm.wasm
    compiler-worker.js
    LICENSE
    THIRD_PARTY_NOTICES.txt
```

Save this as `index.html` and serve `my-site/` using any static HTTP(S) server:

```html
<!doctype html>
<html lang="en">
<meta charset="utf-8">
<title>Pascal in the browser</title>
<script src="pas2wasm/engine.js"></script>
<pre id="result"></pre>
<script>
(async () => {
  const output = document.getElementById('result');
  let engine;
  try {
    engine = await Pas2WasmEngine.create({assetBaseUrl:'./pas2wasm/'});
    const compiled = await engine.compile("program hello; begin writeln('Hello, world') end.");
    if (compiled.diagnostic) {
      output.textContent = JSON.stringify(compiled.diagnostic);
      return;
    }
    const session = engine.run(compiled.binary, {
      mode:'batch',
      onOutput(text) { output.textContent += text; }
    });
    const outcome = await session.done;
    if (outcome.status === 'failed') output.textContent = JSON.stringify(outcome.diagnostic);
  } catch (error) {
    output.textContent = `${error.code || error.name}: ${error.message}`;
  } finally { engine?.dispose(); }
})();
</script>
```

`build/examples/embedding.html` is an independent editable example with batch input,
Stop and recovery. Serve it after `make serve` at `/examples/embedding.html`.

## Initialization, assets and version

`await Pas2WasmEngine.create({assetBaseUrl:'./pas2wasm/', signal})` resolves the directory
relative to the embedding document. Absolute same-origin directories such as
`/project/pas2wasm/` also work under GitHub Pages subpaths. A trailing slash is optional;
queries/fragments, other origins and `file://` compiler loading are rejected.
The optional AbortSignal cancels initialization. Loading and initialization have a
five-second total timeout.

The classic `engine.js` bundle contains the repository-owned wrapper/runtime/controller.
Initialization fetches the compiler loader, Wasm and worker bootstrap once and probes
the actual compiler. Read `engine.version` for its embedded VERSION. Later compiles/runs
use cached assets without network access, including after cancellation. Dispose engines
when removing their owning application/view; initialization failures release resources.

Always deploy all assets from one build/release together. The generated program ABI
remains `pas2wasm_v1`; it is distinct from the compiler version. Compiler workers verify
the reported version against initialization. Mixed runtime/compiler releases are
unsupported; missing imports and corrupt modules produce failed run diagnostics.

## Compile and diagnostics

```js
const compiled = await engine.compile(source, {
  emitWat: true,           // Default false; WAT is otherwise null.
  target: 'wasm32',        // Only implemented target.
  signal: controller.signal
});
```

Success returns `{binary:Uint8Array, wat, target:'wasm32', format:'wasm'}` with owned
artifacts. Enabling WAT preserves binary bytes. A Pascal error returns
`{diagnostic:{code,message,line,column}}` without artifacts. Locations are 1-based lines
and UTF-8 byte columns; zero means unavailable. Render all messages/artifacts as text.
Sources are limited to 256 KiB. Other [compiler limits](runtime-abi.md#compiler-limits)
and language checks remain unchanged.

Calls serialize in invocation order with fresh compiler memory. The five-second compile
timeout includes worker initialization and excludes queue waiting. Abort rejects queued
or active requests with `AbortError`; later requests still work. Use AbortController to
cancel compilation. Expected Pascal diagnostics retain `E_*` codes; transport/startup,
loading, version and timeout failures reject with `Pas2WasmEngineError` and `ENGINE_*`
codes. Invalid JavaScript argument types reject with TypeError.

## Runs, callbacks and lifecycle

`engine.run(binary, options)` accepts generated Uint8Array or ArrayBuffer bytes and
returns `{done, stop, submit}` before callbacks start. Each run gets fresh memory,
runtime state and budgets. One engine permits one active program: overlapping `run`
calls throw `ENGINE_BUSY`. Compilation may coexist with execution. Separate engines
can run independently.

| Option | Contract |
| --- | --- |
| `mode` | `'batch'` (default) or `'interactive'`. |
| `input` | Initial input string, default empty. Batch input is closed and needs no JSPI. |
| `files` | Optional UTF-8 text-file byte records; see below. |
| `onOutput(text)` | Raw program output, excluding human-input echo or terminal rendering. |
| `onStateChange(state)` | `running`, `waiting`, `completed`, `failed`, `stopped`. Running may repeat after input resumes. |
| `onInputRequest(request)` | Interactive line/key request, including source location, prefix and a bound submit function. |
| `onCapabilities({jspi})` | Execution worker's JSPI capability. Unsupported interactive runs fail with `E_JSPI`; batch remains available. |

`session.done` always resolves once: `{status:'completed',files}`, or
`{status:'failed',diagnostic}`, or `{status:'stopped'}`. Output and terminal state
callbacks precede resolution; none occur after termination. Runtime errors retain their
located `R_*` codes. Stop/disposal and timeouts publish no successful file results.
`session.stop()` is idempotent and hard-terminates compute loops and suspended input.
A later run works without reloading the engine. Callbacks must return promptly: their
returned promises are not awaited. Catch errors inside asynchronous application code.

A synchronous callback exception stops execution with `ENGINE_CALLBACK` and zero source
location. If the terminal state callback itself throws, `done` reports that failure
without a second terminal state notification. Malformed run arguments throw before
starting a session. Failed worker construction produces a failed `R_WORKER` outcome.

## Interactive input

```js
const session = engine.run(compiled.binary, {
  mode: 'interactive',
  onOutput(text) { appendRawOutput(text); },
  onInputRequest(request) {
    if (request.kind === 'line') {
      // Show request.prefix as the initial editable draft. Enter submits the
      // complete draft, including the prefix, without a CR or LF delimiter.
      requestLineFromUser(request.prefix).then(text => request.submit(text));
    } else {
      // Return a nonempty raw chunk; null means EOF. No automatic input echo.
      requestKeyFromUser().then(text => request.submit(text));
    }
  }
});
```

The `appendRawOutput`, `requestLineFromUser` and `requestKeyFromUser` functions above
are application UI code. The optional console component below supplies a ready-made
terminal. `read`/`readln` request
editable lines; CRT `readkey` requests raw key chunks, including controls such as
Backspace (`\b`), Enter (`\r`) and Escape (`\x1b`). Keys normalize LF/CRLF to CR.
A key chunk may leave typeahead that becomes a later line request's `prefix`.

Requests expose `requestId`, `kind`, `prefix`, `line`, `column`, and `submit(value)`.
Prefer that bound submission for delayed UI answers: it rejects stale/duplicate requests
with false. `session.submit(text, requestId)` is equivalent; omitting the ID targets the
current request. `submit(null)` permanently closes input, preserving already queued
bytes. Empty strings are valid lines, not key chunks. Invalid line delimiters/empty keys
fail with `R_INPUT_INVALID`; invalid value types throw TypeError without consuming the
request. Submitting with no pending request or after completion returns false.

Use JSPI-capable browsers for live suspension; do not replay programs or silently switch
to batch. Applications can offer an explicit batch fallback. Human waits do not count
toward the five-second cumulative active budget; submitting input never replenishes it.
Console input and raw output each retain a 1 MiB allowance. Memory is capped at 64 MiB
for compiler-generated programs, with checked routine/frame/heap limits.

## Reusable console

Copy `console.js` from `build/` alongside `engine.js` and load it with a classic script
include. It bundles the renderer and component, uses no framework or fetches, and can
also be used without the engine by supplying equivalent callbacks/requests.

```js
const terminal = Pas2WasmConsole.create(document.getElementById('my-console'), {
  autoFocus: true
});
const handlers = terminal.createHandlers();
const session = engine.run(compiled.binary, {
  mode: 'interactive',
  ...handlers,
  onStateChange(state) {
    handlers.onStateChange(state);
    showStatus(state); // Application UI.
  }
});
const outcome = await session.done;
// The application owns Stop, diagnostics, files, and engine cleanup.
if (outcome.status === 'failed') showDiagnostic(outcome.diagnostic);
terminal.dispose();
```

The component appends its own light-DOM subtree to the supplied element and preserves
other host content. It contains a scrolling terminal, protected output, an inline native
editor. It adds no buttons, mode selector, file panel or compiler. Lines echo
literally on Enter; raw keys do not echo. Program-output Backspace retains the existing
[scalar cursor behavior](spec.md#backspace-output-display). Messages are rendered as text.

The component provides no EOF method or keyboard shortcut, including Ctrl+D. The
playground and exported HTML use Enter for lines and application-owned Stop to cancel
execution; programs requesting further input keep waiting. Headless applications can
still send EOF with the engine's `session.submit(null)` or `request.submit(null)`.
Batch exhaustion and virtual-file EOF are unchanged.

| Method/option | Contract |
| --- | --- |
| `createHandlers()` | Start a fresh connection, clearing output/draft and invalidating previous callbacks. Returns `onOutput`, `onInputRequest`, `onStateChange`; stop a previous application-owned session before rebinding. Terminal states invalidate this connection and disable editing. |
| `clear()` | Clear output without changing a pending input request, draft, caret or focus. |
| `focus()` | Focus an active editor, preserving its existing selection. Inactive consoles do not acquire focus. |
| `autoFocus` | Boolean, default true. False suppresses input-request-driven focus; explicit focus/user clicks still work. |
| `dispose()` | Idempotently invalidate callbacks, remove listeners and owned DOM/styles. It does not stop/dispose application sessions or engines. Other public calls afterward throw `InvalidStateError`; delayed event callbacks are inert. |

Each widget has independent state and no fixed element IDs. Requests require the engine's
positive increasing `requestId`, kind/prefix and bound `submit` function. Duplicate/stale
requests are ignored. Keep your status/diagnostics outside the component and forward
state changes if wrapping its handlers. Batch runs can use the same output callbacks;
interactive suspension still requires JSPI and unsupported runs retain `E_JSPI`.

Styles are scoped to each widget and removed with it. Override these inherited CSS
variables on the host container (values shown are defaults):

```css
#my-console {
  --p2w-console-background: #18362f;
  --p2w-console-color: #f4f2e8;
  --p2w-console-caret-color: #ffd58a;
  --p2w-console-font: .9rem/1.65 ui-monospace, monospace;
  --p2w-console-min-height: 160px;
  --p2w-console-max-height: 360px;
}
```

The bundle inserts a scoped style element per widget. CSP must permit those styles
under the same inline-style constraints as the current playground/offline renderer.
A separate strict-CSP style delivery mode remains future work. Replacing scoped styles
with host-wide selectors can affect native editing layout and should be tested.

`build/examples/console.html` demonstrates two independent engines/consoles and
application-owned source, Run/Stop and mode controls. The playground and standalone
exports use this same component; exported HTML embeds its code/styles without fetching
`console.js`, compiler assets or any sibling files.

## Text files and ownership

```js
const session = engine.run(compiled.binary, {
  files: [{name:'input.txt', data:new TextEncoder().encode('hello\n')}]
});
const outcome = await session.done;
if (outcome.status === 'completed') {
  for (const {name, data} of outcome.files) consumeFile(name, data);
}
```

Inputs are snapshotted before `run` returns without detaching caller buffers. The runtime
validates names, sizes and UTF-8 atomically. Supplied/generated files share 32 names and
1 MiB total live contents; names are case-sensitive, 1..255 UTF-8 bytes, NUL-free and
unique. Empty/BOM/NUL/CRLF/control contents retain exact bytes. Only files last rewritten
as text become successful caller-owned snapshots, including empty files and rewritten
inputs. Unchanged inputs and Pascal typed files are excluded. Pascal never receives PC
paths or persistent storage. Applications own selection/download controls and must render
names as inert text. See [host interchange](runtime-abi.md#host-text-file-interchange).

## Cleanup, privacy and CSP

`engine.dispose()` stops the active session, aborts active/queued compiles and releases
workers, timers, listeners, cached assets and Blob URLs. Repeated calls are harmless.
Previously returned artifacts/snapshots remain caller-owned. Later compile calls reject
and run calls throw `ENGINE_DISPOSED`.

Source/input/files remain in worker messages and local memory. All asset requests are
same-origin static GETs; no source is submitted to a server. Generated programs receive
only the checked runtime imports, with no network/DOM/host-filesystem capability.
Serve trusted, matching assets; embedding applications remain responsible for their own
scripts and callback behavior.

Delivery uses classic scripts, Fetch, Blob workers and WebAssembly. CSP must permit the
engine script, same-origin asset fetches, Blob workers and Wasm compilation in workers
(for example appropriate `script-src 'self' 'wasm-unsafe-eval'`, `connect-src 'self'`,
`worker-src blob:` policies). Inline application examples also need your site's usual
nonce/hash or an external application script. A static-worker delivery mode is deferred;
there is no eval-based main-thread module loader.

This compiler embedding requires HTTP(S). [Standalone HTML exports](spec.md#standalone-html)
embed an already compiled program and matching runtime/UI, work offline through `file://`,
and do not include this compiler engine or fetch any sibling assets.

## Compatibility and verification

The documented API follows the project's [release policy](contributing.md#versioning-and-releases),
including possible minor-version breaking changes before 1.0. Diagnostic wording, WAT
formatting and bytes across different compiler releases are not compatibility promises.
The low-level [compiler/runtime ABI](runtime-abi.md) remains available.

`make test-embedding` exercises the actual distribution under a nested URL in Chromium
and Firefox, the independent example, these complete HTML quick starts, lifecycle,
files and JSPI/fallback. It is also part of `make test-browser`. Reports in
`build/embedding-{chromium,firefox}.json` record raw/gzip asset sizes, initialization,
fresh/repeated compilation, a comparison warm worker, program sizes, run startup and
retained workers/Blob URLs. Timings are local measurements, not browser-independent
performance promises. No portable cross-browser total worker-memory API is available;
resource accounting verifies owned workers/URLs rather than claiming exact heap sizes.
