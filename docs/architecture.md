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

Declaration sections and routine bodies are parsed in source order in every scope;
later declarations do not change bindings already resolved in earlier bodies. Global
write summaries use fixed bounded bitsets covering all possible global slots, so adding
globals after a routine never invalidates its effect storage or closure computation.

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

Boolean comparisons retain Boolean operand types and use ordinal `false < true`
ordering in both constant evaluation and lowering; they introduce no numeric coercion.
Mixed character/string relations insert the same explicit one-scalar string widening
used by concatenation, reusing lexical comparison and its alias-safe snapshots.
Decimal numeric character literals are validated as Unicode scalars by the lexer and
become ordinary typed character constants; runtime layouts and imports remain shared.

Enumeration constructors own nominal identities and ordered member names. Member
constants occupy ordinary lexical declarations without runtime storage. Arrays retain
a separate ordinal index domain, so enum/character/Boolean domains share checked array
lowering while mixed-domain indexes fail in the frontend. Enum output converts to
immutable string values via one backend-owned name table per type; no target offsets
are stored in the IR. Explicit parameter-symbol maps preserve formal positions when
inline enum members introduce constants between parameter groups.

Set storage identities are interned by ordinal base and inclusive bounds. Constructor
expressions retain their base until a value context supplies bounds; explicit conversion
nodes check changes between domains. The frontend folds logical membership bits, while
the backend owns bitset object headers and byte addressing. Consumed operands are copied
before later calls, releasing their heap borrows once the copy is complete. Constructors,
conversion, and algebra build private bounded results, preserving atomic destination
writes without new host services.

A `with` binding is a logical record alias, resolved through an explicit IR identity
rather than substituted source text. The parser's temporary field namespace prefers
the nearest binding. Lowering captures address and owning root once, saving arena and
borrow floors for the body's statements. Nested scopes retain their owners during
calls/input waits; loop contexts identify which bindings survive a transfer. Outward
branches unwind only exited scopes before their temporary storage can be reused.

Variant declarations retain logical owners, ordered arms/labels and one field namespace;
anonymous arm bodies have independent physical field lists. Layout overlaps those bodies,
with a separate storage-arm header from the ordinary Pascal tag. Backend schemas assign
canonical field identities without emitting compiler pointers or placing offsets in IR.

Captured paths retain ancestor view claims until read/commit. The memory-only runtime
checks immutable schemas, builds an activation plan against virtual ancestor clears,
and snapshots copy sources before touching storage. Compatible fields survive arm
changes; incompatible incoming fields and nested parts are cleared. This preserves real
union aliasing while preventing stale string headers/pointer bits from acquiring a new
type. Direct writes stay atomic. Writable bindings activate/pin views; their external
lease chain protects references, character views and with scopes without changing the
existing two-word reference calling convention. Private copies carry view/initialization
state but no leases. Scope/transfer cleanup shares the existing frame ownership path.

Subrange storage retains a nominal descriptor with base type and constant bounds.
Expression types expose the base scalar, and explicit range-check nodes retain the
destination constraint for narrowing. References compare declared lvalue types rather
than expression types. Input/increment stores check after parsing/arithmetic and before
committing, preserving scalar initialization and atomic failure behavior.

## Lvalues, frames, and value semantics

Loads and writable storage paths are explicit typed IR nodes. The parser retains
writability separately from optimized expression values: parentheses preserve a path,
unary signs do not, and returned aggregate/string temporaries stay read-only. A returned
pointer can still select its live writable pointee.

Checked `inc` has an explicit storage-update statement rather than duplicating its
target in a synthetic assignment. Lowering captures the path once, applies ordinary
control/initialization checks, and commits the increment only after overflow checks.
The existing selector leases and owning-result root carry alias and lifetime behavior.

Output roots retain optional typed width/precision expressions. Lowering snapshots the
value and consumes its borrows before those expressions execute, retaining operands on
the Wasm stack until one formatted-write import. The self-contained host rounds exact
binary64 rationals with bounded built-in BigInt arithmetic for fixed decimals, then
checks Unicode padding and the complete UTF-8 field against the output budget.

Random operations lower to two synchronous runtime services. Each host owns a bounded
u32 xorshift state; rejection sampling maps its nonzero cycle into unbiased bounded
integers. A host seed override enables exact cross-engine/WAT tests while production
randomize uses the browser's built-in random source. No state leaks between runs.

Comma-separated array constructors and indexes normalize in the frontend to ordinary
nested descriptors and selector chains. Each dimension retains its source location and
consumes the existing depth budget. The backend uses the same checked child strides,
copies, reference paths, and initialization flags without a separate matrix layout.

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
result writes. Address-of captures their source/index in a checked identity; each later
dereference reconstructs a temporary descriptor and rechecks the owner lifetime.

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
JavaScript does not allocate Pascal heap objects. Generational handles prevent
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

## Address identities

`reference.c` lowers logical `@storage` without exposing raw coordinates to Pascal.
Legacy allocation handles remain unchanged. Nonheap/interior references use tagged i64
identities in a bounded, per-runtime JavaScript registry. The registry stores only
checked coordinates, owning roots/lifetimes and view claims, never Pascal payloads or
host capabilities. Globals last for the run; monotonic activation epochs and allocation
generations prevent stale references from reviving when storage is reused. Frame return
retires only that owner's identities, preserving forwarded caller aliases.

Compatible overlapping fields normalize by canonical declared type and offset, so
aliases share identity without giving incompatible containing views the same identity.
A stable representative arm defines later reactivation. Stored pointers retain claims
without permanent leases. Dereferencing borrows any heap owner and carries dynamic
claims into existing read/binding/atomic-commit services; a pointer cannot bypass a
subrange, counted-control, initialization, or variant check. Binding preflights expanded
lease capacity before activation and reuses the request buffer after snapshotting it.
This keeps frontend/IR independent of Wasm layout and avoids a second allocator or
unbounded host ownership. Standalone exports embed the same self-contained runtime.

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

## Planned virtual filesystem

Planned Pascal file I/O uses bounded in-memory storage owned by one program execution.
Each Run starts with an empty filesystem, including repeated Runs in the same HTML
session. Closing a file releases its open handle but preserves its named contents, so
the program can reopen it during that execution. Completion, failure, Stop, or timeout
releases all file contents and handles.

Filenames identify virtual files and grant no access to the PC's filesystem. The initial
design has no persistence between executions, browser storage, or file import/download.
The same execution-scoped behavior must work with either compiler host and in standalone
offline HTML, without external dependencies. This records the agreed storage lifetime;
file types, operations, encoding, limits, and diagnostics still require their design gate.
File I/O remains planned rather than currently supported.

## Planned CRT compatibility

CRT support is planned as a built-in compatibility unit exposing a documented subset,
initially `readkey`. The compiler will emit the required CRT helpers directly into each
generated program, with matching optional WAT. A separate precompiled Wasm CRT library
is not required initially. Browser services such as keyboard events still use narrow
host-runtime imports; emitted Wasm helpers do not access the DOM directly.

If CRT support grows, its helpers can be extracted into a separate module while
preserving the Pascal-facing interface. Accepting `uses crt;` will expose only supported
declarations, without implying full CRT compatibility; unavailable routines remain
compile-time errors. This describes future implementation direction, not current
language support.

## Platform references

- [WebAssembly core specification](https://webassembly.github.io/spec/core/) and
  [JavaScript API introduction](https://webassembly.org/getting-started/js-api/).
- [Emscripten overview](https://emscripten.org/docs/introducing_emscripten/about_emscripten.html).
- [Local-file origin restrictions](https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/CORS/Errors/CORSRequestNotHttp)
  and [embedded workers](https://developer.mozilla.org/en-US/docs/Web/API/Web_Workers_API/Using_web_workers#embedded_workers).
- [JSPI and Asyncify concepts](https://emscripten.org/docs/porting/asyncify.html):
  compiling the compiler to Wasm does not give suspension to generated programs;
  their input suspension is provided by the JavaScript host using JSPI.
