# Pas2Wasm architecture

This document explains the current implementation and its design rationale. [Product
requirements](spec.md), [language semantics](supported-language.md), [exact
interfaces/layouts](runtime-abi.md), and the [development workflow](contributing.md) are
maintained separately. Historical implementation checkpoints and verification reports
are retained in Git history.

## Compiler pipeline and module boundaries

```text
Pascal source -> lexer/parser -> semantic analysis -> typed representation
                                                           |
                                                    backend interface
                                                           |
                                                    Wasm lowering
                                                   /             \
                                              binary          optional WAT
```

| Module | Responsibility |
| --- | --- |
| `src/frontend/` | Lexing, parsing, name/type/lvalue checks, constant folding, lexical scopes, and call effects |
| `src/ir/` | Logical types, expressions, statements, resolved storage paths, and bounded numeric helpers |
| `src/backend/` | Internal, statically registered backend dispatch and target validation |
| `src/backend/wasm/` | Concrete layouts, calling convention, checked heap, lowering, and binary/text encoding |
| `src/compiler.c`, `src/pas2wasm.h` | Memory-only compilation, artifact/diagnostic ownership, and version accessor |
| `src/driver/` | Native file/CLI adapter |
| `web/` | JavaScript compiler wrapper and example browser integration/runtime/exporter |

Both hosts use the same frontend and backend source lists. The native executable is a
compiler host, not a native-code output backend. Logical descriptors contain no Wasm
widths, offsets, opcodes, imports, or JavaScript objects. The backend interface accepts
a validated program and options and produces owned artifacts through the common compiler
result; adding a target must not rewrite the frontend. Frontend/IR tests link without
the Wasm emitter or browser runtime.

## Names, types, constants, and call effects

The parser uses bounded declaration chains and child-routine lists per lexical scope,
avoiding repeated scans of unrelated scopes. Routines also have a flat list with stable
backend indices. Unified lookup selects the nearest declaration across
value/type/routine kinds; implicit function results keep their associated callable
identity. Record members have separate namespaces.

A forward registers a stable signature rather than permanent parameter bindings.
Temporary header bindings are rolled back; the definition installs its own names and
location. Explicit value/reference modes and canonical types are compared. A separate
effects pass computes bounded transitive reachability using a two-word Warshall closure,
then unions reachable global writes. This catches writes hidden by recursive cycles and
validates calls after all bodies are known.

Type aliases reuse logical identity. Constructed arrays/records retain nominal identity;
strings are interned by capacity and pointers by resolved target. Pending pointer
targets are arena records, not variable symbols. Older-first bounded unions normalize
aliases without traversing pointer cycles; every type, including anonymous/unused
pointees, is registered for checked layout.

Constants occupy the ordinary namespace without runtime variable slots. Typed
expressions fold left-to-right with checked integer intermediates and bounded text
storage. Real literals use exact integer rationals and ties-to-even rounding,
independent of locale or `strtod`; stored binary64 intermediates and bitwise conversions
avoid excess precision and external libm/parser dependencies. Widening is explicit in
typed nodes. Character folding uses owned bounded UTF-8 helpers.

## Lvalues, frames, and value semantics

Loads and writable storage paths are explicit typed IR nodes. The parser retains
writability separately from optimized expression values: parentheses preserve a path,
unary signs do not, and returned aggregate/string temporaries stay read-only. A returned
pointer can still select its live writable pointee.

The backend captures each destination path before evaluating its source or consuming
input. Indexes are checked before address arithmetic. Scalar loads test the selected
initialization flag; aggregate loads provide copy addresses without reading unwritten
leaves. Overlap-safe copies include all value bytes and leaf flags, while
control/lifetime identity stays outside copied payloads.

Globals and routine values are addressable linear-memory objects, making aliases,
captures, and partial initialization use the same design. Each call clears a fresh
bounded frame. Each function has an initialized/written result root; descendant writes
mark that root without marking unrelated leaves. Return paths share one epilogue that
validates results and restores the frame once. Caller-owned depth accounting and
protection/borrow checkpoints survive early returns.

Arguments are snapshotted as they evaluate, before later arguments can mutate sources.
Callee value parameters own independent copies. Aggregate/string results use a
caller-owned hidden destination, copied before the callee frame is released. Checked
temporary leases share the frame arena: argument leases end after calls, result leases
after consumption, and statement/condition/output cleanup prevents accumulation across
iterations and long argument lists. String snapshots reserve actual bytes; declared
storage reserves full capacity, so typed copies must not read a larger declared capacity
from a shorter snapshot.

## References, captures, and protected counters

A reference carries an actual address plus its owning result-root address, without
loading the value. This supports output initialization, repeated aliases, forwarding,
and writes through captured/result aggregates. Nested calls pass the lexical parent's
live frame; sibling/recursive calls follow static links to the proper parent. Captured
references resolve in their owning activation. There is no global current-frame
singleton or escaping routine environment.

Static checks reject known active-counter writes and transitive global effects. Runtime
interval guards cover hidden aliases, containing aggregates, nested counter
initialization, and captured/heap storage. Source writes and reference bindings consult
an external active-control address stack; compiler-owned loop steps/cleanup bypass it.
Returns restore entry depth while preserving caller controls. This avoids interpreting
string bytes as protection flags or copying protection identity with values.

String-character references use caller-owned tagged descriptors containing the string
object and scalar position. Each access rereads the live string and rechecks bounds
after aliases change lengths or byte widths. The normal reference root pair propagates
result writes; views cannot escape their owner's lifetime.

## Control flow and Wasm emission

Binary and optional WAT use the same lowering/primitives and import descriptor table.
Float constants share exact bits, rendered as hexadecimal WAT. Text output is a
representation of the Wasm backend, not a separate backend or assembly stage.

Structured loops retain distinct bound/label state for nesting and reuse scratch state
for siblings. Counted bounds are captured before initializing the counter; completion
checks precede stepping at integer endpoints. Absolute Wasm label depths keep loop
transfers correct through nested conditionals. Value exits assign the result before
using the shared epilogue. Case selectors live in temporaries; interval comparisons
avoid expanding ranges or overflowing endpoint differences.

Finite-result and narrowing checks precede uses and potentially trapping Wasm
conversions. Exact runtime import signatures, generated failure codes, storage sizes,
hidden arguments, and resource limits are in the ABI reference.

## Checked heap and lifetime ownership

The Wasm backend emits private allocation/validation/borrow/release helpers in `heap.c`
using shared binary/WAT primitives from `emitter.h`. Ownership stays in program memory;
JavaScript has no allocator or persistent object registry. Generational handles prevent
disposed aliases from becoming valid after reuse. Whole-block first-fit reuse and
checked bump growth are deliberately simple; fragmentation can exhaust the budget
without promising compaction/coalescing.

Dereferences pin allocations through expression/call lifetime leases. Value snapshots
end their borrows before later arguments; writable references, captures, and character
views preserve caller borrows until return. Conditions release borrows before entering a
body. Cleanup unpins before restoring frame/lease storage. Active-counter checks precede
lifetime rejection when both apply; pointer aliases alone do not borrow. Test-only WAT
exports reach private boundary helpers without adding capabilities to production
modules.

## Browser compilation, execution, and input

The example page fetches same-deployment compiler assets once, caches them in memory,
and supplies their bytes/source to replacement workers. The Emscripten loader explicitly
enables incoming `wasmBinary`; workers do not need a filesystem or a second asset fetch.
The JavaScript wrapper copies artifacts before freeing C result/source storage. VERSION
generates a shared build header, so both hosts and the loaded UI report one embedded
identity without a runtime version file.

Compiler and program workers are separately terminable and have separate memories. Raw
generated modules export only their entry point/memory and execute only when the host
invokes them. Each execution owns its input cursor and output limits; failed, stopped,
or timed-out instances are discarded rather than resumed.

A runtime owns a line queue, offset, and closed/open provider. Batch imports remain
synchronous. Only fetching an unavailable interactive line may suspend; buffered
operations retain synchronous behavior. All modes share parsing/storage helpers. The
parser lowers multi-field input into sequential reads, with one final discard for
`readln`; `write` uses output helpers without a newline call.

The execution worker wraps potentially waiting imports with browser JSPI and awaits the
same program's entry point. The module itself is unchanged. It flushes output before an
input request; the page's cumulative watchdog charges running intervals and pauses human
waits. Run/request identities prevent stopped sessions or duplicate submissions from
delivering input to a new execution. The ABI reference owns message shapes and provider
contracts.

## Inline console and offline export

The shared terminal renderer keeps a persistent read-only history text node and an
adjacent plaintext editing root in one scrolling `pre`. Appending history does not
replace draft nodes or selection. Native browser editing handles Unicode; boundary
checks protect history, and composition guards prevent an IME-confirming Enter
(including a late line-break event) from submitting. Empty editors reserve a minimal
caret box using CSS without inserting input data. Paste is plain text; scroll following
respects existing history selection and scroll position.

Renderer, console/session, runtime, and worker functions are embedded in exported HTML
using their function source. Base64 module data prevents source strings from breaking
script markup. All required styles are embedded; fresh local-file runs need no sibling
requests, cached deployment, special headers, or compiler binary. Worker/Blob URL
ownership supports Stop and cleanup in both integrations.

The full MIT notice is embedded as a static HTML comment and copied separately into the
built site. Browser checks compare the embedded text to the canonical root LICENSE,
preventing drift without adding a build-time code generator or runtime fetch.
Third-party support notices accompany compiler assets; the standalone export contains
only the program and repository-owned host code.

## Platform references

- [WebAssembly core specification](https://webassembly.github.io/spec/core/) and
  [JavaScript API introduction](https://webassembly.org/getting-started/js-api/).
- [Emscripten overview](https://emscripten.org/docs/introducing_emscripten/about_emscripten.html).
- [Local-file origin restrictions](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CORS/Errors/CORSRequestNotHttp)
  and [embedded workers](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Using_web_workers#embedded_workers).
- [JSPI and Asyncify concepts](https://emscripten.org/docs/porting/asyncify.html):
  compiling the compiler to Wasm does not give suspension to generated programs;
  their input suspension is provided by the JavaScript host using JSPI.
