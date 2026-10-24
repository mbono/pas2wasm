# Pas2Wasm

Pas2Wasm is a Pascal-to-WebAssembly compiler written in portable C. The compiler
itself builds as both a **native executable** and a **WebAssembly module**:

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

## Use the compiler in a browser

Build with `make wasm` to obtain `pas2wasm.js`, the compiler loader, and
`pas2wasm.wasm`, the compiler itself. These artifacts can be embedded in your own
web application. The [JavaScript wrapper](web/compiler.js) accepts Pascal source
and returns compiled Wasm bytes or a compilation diagnostic. The
[C API](src/pas2wasm.h) provides the underlying memory-only compiler interface.

To execute a compiled program, instantiate its Wasm module with the included
[JavaScript runtime](web/runtime/host.js), attach its memory, and call its `run`
export. Your application supplies input and receives output through the runtime.
The [runtime documentation](docs/runtime-abi.md) describes integration and limits.

The example playground in `web/` demonstrates the complete integration, including
separate compiler/program workers, cancellation, interactive and batch input,
optional WAT, and standalone HTML export.

### Try the example playground

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
type `2 3` after the prompt and press Enter to see `Sum: 5`. **End input** signals
that no more input is available; **Stop** cancels a running or waiting program.
Interactive input requires built-in WebAssembly JSPI support. Batch input works
without JSPI.

For **Batch input**, select that mode, enter `2 3` in the input box before Run,
and run the program. `readln(n, m)` expects both numbers on the same line,
separated by spaces or tabs. Use separate `readln` statements to consume separate
lines, or `read(n, m)` to read numbers across line boundaries.

### Export a program that runs offline

After compiling in the example playground, choose **Download runnable HTML**.
Open the single downloaded file directly in Chromium or Firefox and press
**Run program**. It includes the compiled program and its runtime, supports
interactive and batch input, and needs no server, installation, sibling files,
or network connection. Interactive input requires browser JSPI support; batch
mode remains available. The playground also offers raw Wasm downloads and
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

- [Contributor and developer guide](docs/contributing.md): architecture, tests, and hosting.
- [Changelog](docs/changelog.md): release history.
- [License](LICENSE): repository-owned code and documentation use MIT.

You choose the license for your own Pascal programs. Redistributed Pas2Wasm
runtime code, including the runtime embedded in exported HTML, retains its MIT
notice. Third-party components retain their own licenses and
[notices](docs/third-party-notices.txt).
