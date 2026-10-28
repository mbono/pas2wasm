# Pas2Wasm

Pas2Wasm is a **Pascal-to-WebAssembly compiler with first-class browser integration**,
written in portable C. The compiler itself builds as both a **native executable** and a **WebAssembly module**:

| Compiler build | Artifacts | Usage |
| --- | --- | --- |
| Native | `pas2wasm` | Compile Pascal from the command line or integrate the C API. |
| WebAssembly | `pas2wasm.wasm` and `pas2wasm.js` | Embed the compiler in a web application to compile Pascal directly in the browser. |

Both builds compile the same Pascal subset and emit WebAssembly programs. With
the included JavaScript runtime, you can compile **and run Pascal entirely in a
browser**, without a compiler server. Source and input stay in the browser.

The repository includes a playground demonstrating how to integrate the compiler,
run generated programs, and provide console input/output and downloads.
Try the [online playground on GitHub Pages](https://mbono.github.io/pas2wasm/)
without installing any tools.

## Embed compilation and execution in a browser

Build with `make site`, then copy `engine.js`, `pas2wasm.js`, `pas2wasm.wasm`,
`compiler-worker.js`, `LICENSE`, and `THIRD_PARTY_NOTICES.txt` from `build/` into a
`pas2wasm/` directory beside your HTML page. Serve these static files over HTTP(S):

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

The headless API runs compilation and programs in separate workers. It supports batch
input, interactive line/key callbacks, optional WAT, cancellation and text-file exchange.
See the [browser embedding guide](docs/browser-embedding.md) for the full contract and
[standalone integration example](web/examples/embedding.html). No packages or compiler
server are required. Each engine permits one active program; create separate engines
for independent concurrent examples.

The optional `console.js` bundle provides the existing interactive terminal as a
reusable component with inline input. Applications keep their own
Run/Stop controls, editors and file UI. See the [console API](docs/browser-embedding.md#reusable-console)
and [two-console example](web/examples/console.html).

## Build the compiler

You need GNU Make 4.3+ and a C11 compiler. Building the WebAssembly compiler also
requires Emscripten. From the repository directory, run:

```sh
make                     # Build both compiler hosts and the example playground
```

The outputs are in `build/`. You can also build individual components:

```sh
make native              # Native compiler: build/pas2wasm
make wasm                # Browser compiler: build/pas2wasm.js and build/pas2wasm.wasm
make site                # Browser compiler and complete example playground
make clean               # Remove generated files
```

No JavaScript packages or external runtime downloads are required.

## Compile Pascal from the command line

Save this program as `sum.pas`:

```pascal
program sum;
var
  n, m: integer;
begin
  write('Insert two numbers: ');
  readln(n, m);
  writeln('Sum: ', n + m)
end.
```

Compile it using the native compiler:

```sh
build/pas2wasm -o sum.wasm sum.pas
build/pas2wasm --emit-wat -o sum.wasm sum.pas  # Also write sum.wat
build/pas2wasm --version
build/pas2wasm --help
```

Without `-o`, the compiler writes `program.wasm`. WAT is the readable text form
of WebAssembly and is emitted only with `--emit-wat`.

A raw `.wasm` file needs a host runtime; opening it directly in a browser does
not run the Pascal program. The included browser runtime supplies console I/O
and the other [host imports](docs/runtime-abi.md) that generated programs need.

## Example playground

Open the [hosted playground](https://mbono.github.io/pas2wasm/) in Chromium or
Firefox to compile and run Pascal directly in your browser. It demonstrates the
compiler's browser integration, including console I/O and downloadable programs.

To run the playground locally, with Python 3 installed, run:

```sh
make serve
```

Open <http://localhost:8000> in Chromium or Firefox. The local server serves static
files; compilation and execution happen in your browser. After the assets load,
compilation and execution need no network connection.

Paste the example above and press **Run**. **Interactive console** is the default:
type `2 3` after the prompt and press Enter to see `Sum: 5`. **Stop** cancels a running
or waiting program. There is no manual EOF control or Ctrl+D shortcut; a program
requesting further input waits for another submission or Stop.
Interactive input requires built-in WebAssembly JSPI support. Batch input works
without JSPI.

For **Batch input**, select that mode, enter `2 3` in the input box before Run,
and run the program. `readln(n, m)` expects both numbers on the same line,
separated by spaces or tabs. Use separate `readln` statements to consume separate
lines, or `read(n, m)` to read numbers across line boundaries.

### Use program files

The playground's **Files** panel accepts UTF-8 input files before Run. Match the selected
filename exactly in Pascal, for example `assign(t, 'input.txt'); reset(t);`. Each Run
starts from fresh copies of your selections; remove individual inputs or use **Clear
inputs** to change them.

After a successful run, the panel lists generated text files with **Download** buttons.
Downloads preserve the exact file bytes. Uploaded and generated files share a limit of
32 names and 1 MiB of contents; the panel shows the allowance available at Run start.
Typed files have no external interchange format. Pascal cannot access PC paths directly.

### Export a program that runs offline

After compiling in the example playground, choose **Download runnable HTML**.
Open the single downloaded file directly in Chromium or Firefox and press
**Run program**. It includes the compiled program and its runtime, supports
interactive and batch input, and needs no server, installation, sibling files,
or network connection. Interactive input requires browser JSPI support; batch
mode remains available. The exported runner also supports text-file selection and
downloads; it starts with no files and does not embed your playground selections.
The playground also offers raw Wasm downloads and
optional WAT viewing/downloading.

## Supported language and limits

Supported features include integers, Booleans, reals, Unicode characters, bounded
strings, fixed arrays, records, typed pointers, procedures/functions, nested
routines, recursion, structured loops and conditionals, and `read`/`readln` and
`write`/`writeln`. This is a Pascal subset, with checked arithmetic, memory access,
and uninitialized reads. See the [language reference](docs/supported-language.md)
for syntax, semantics, and restrictions.

Programs are limited to 256 KiB of source, 1 MiB each of input/output, 64 MiB of
program memory, 64 simultaneous routine calls, and five seconds of active
execution. Time spent waiting for human input is excluded. Compilation also has
a five-second limit.

## Further information

- [Browser embedding](docs/browser-embedding.md): public API, input/output, files, and cleanup.
- [Runtime ABI](docs/runtime-abi.md): low-level C/JavaScript integration and generated-module contracts.
- [Architecture](docs/architecture.md): design and ownership.
- [Contributor and developer guide](docs/contributing.md): architecture, tests, and hosting.
- [Changelog](docs/changelog.md): release history.
- [License](LICENSE): repository-owned code and documentation use MIT.

The motivating application is the author’s [Pascal tutorial](https://github.com/mbono/pascal_tutorial).
Its 23 pinned examples are regression-tested; integrating execution into the tutorial
is separate work.

You choose the license for your own Pascal programs. Redistributed Pas2Wasm
runtime code, including the runtime embedded in exported HTML, retains its MIT
notice. Third-party components retain their own licenses and
[notices](docs/third-party-notices.txt).
