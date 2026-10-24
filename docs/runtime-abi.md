# Compiler and runtime ABI

This reference owns exact compiler integration, generated-module interfaces, backend
layouts, limits, failure mappings, and worker protocol. The [language
reference](supported-language.md) defines Pascal behavior;
[architecture](architecture.md) explains implementation rationale. Layouts and hidden
parameters below describe the current Wasm backend, not source-level addresses or a
native CPU ABI.

## Contents

- [Compiler integration](#compiler-integration)
- [Compiler limits](#compiler-limits)
- [Generated module interface](#generated-module-interface)
- [Runtime diagnostics](#runtime-diagnostics)
- [Storage layouts and calling conventions](#storage-layouts-and-calling-conventions)
- [Host input provider](#host-input-provider)
- [Worker protocol and sessions](#worker-protocol-and-sessions)
- [ABI compatibility](#abi-compatibility)

## Compiler integration

### Native CLI and C ownership

The native adapter accepts `--target=wasm32` (the default and only target),
`--emit-wat`, `--diagnostics-json`, `-o`, `--help`, and `--version`. Without `-o`, the
binary is `program.wasm`; optional WAT uses the same basename. Invalid programs produce
no artifacts; structured diagnostics go to stderr and failure returns a nonzero exit
status. Unsupported targets are rejected explicitly.

The [C header](../src/pas2wasm.h) is the declaration-level API contract:

| Interface | Ownership / result |
| --- | --- |
| `p2w_compile(source, length, emit_wat, target)` | Borrows source bytes/target during the call; NULL target selects `wasm32`. Returns an owned result, including on compilation failure; NULL means result allocation failed. |
| Result/artifact accessors | Borrow binary, optional UTF-8 WAT, and diagnostic storage until `p2w_free_result`. Empty diagnostic code means success; nonempty code means failure without artifacts. |
| `p2w_free_result(result)` | Releases result/artifacts, accepting NULL. |
| `p2w_version()` | Static embedded VERSION string; do not free it. |

Successful artifacts identify target `wasm32` and format `wasm`; WAT is NULL unless
requested. Diagnostic lines/UTF-8 byte columns are 1-based; zero means no location.
Source text remains borrowed rather than becoming a runtime filesystem dependency.

### JavaScript wrapper

[createPas2WasmCompiler](../web/compiler.js) accepts the generated Emscripten factory
and optional module options, returning a Promise of a callable compiler:
`compile(source, emitWat = false, target = 'wasm32')`. Success returns `{target, format,
binary, wat}`; compilation failure returns `{diagnostic: {code, message, line,
column}}`. It copies artifacts before freeing C result/source storage. `.version` is
read-only and comes from `p2w_version()` in the loaded module, not from a runtime file.
Native `--version` prints `Pas2Wasm <version>`. The example heading and browser tab show
the loaded identity. Compiler version does not version generated Pascal programs or the
ABI namespace.

## Compiler limits

| Resource | Bound |
| --- | --- |
| Source bytes | 256 KiB |
| Declarations (variables/constants/types) | 4,096 |
| Arena allocations | 65,536 |
| Nesting / expression and combined selector depth | 128 |
| Routine identities | 128; forwards/bodies count once |
| Explicit parameters per routine | 64; hidden backend arguments do not count |
| Variables per routine | 256, including parameters and implicit result |
| Fields per record | 4,096 |
| Singleton/range case labels | 1,024; intervals do not expand |
| Set domain | At most 256 consecutive ordinals |
| Decoded/folded compiler text | 8 MiB shared; each string value/capacity at most 1 MiB |
| Pointer interning/forward resolution | 8,388,608 comparisons |
| Real literal conversion | 768 significand digits; exponent magnitude 4,096 |

Complexity failures use `E_LIMIT`; invalid capacities, numeric conversions, or physical
layouts use the language reference's specific diagnostics. Constants have no runtime
variable slot; used string constants may occupy literal data.

## Generated module interface

Generated modules import only `pas2wasm_v1`. They export `run: () -> ()` and `memory`,
with no start function. Host code attaches the instance/memory and calls run explicitly.
Memory is capped at 64 MiB; never pass the compiler's memory. Imports validate bounds
before reading/writing program memory and provide no filesystem, network, DOM, or
arbitrary-JavaScript access.

### Existing imports (indices 0–22)

| Import | Wasm signature | Behavior |
| --- | --- | --- |
| `write_utf8` | `(i32 pointer, i32 length) -> ()` | Validate bounds, decode UTF-8, append text |
| `write_i32` | `(i32 value) -> ()` | Append decimal text |
| `write_bool` | `(i32 value) -> ()` | Append `TRUE` or `FALSE` |
| `newline` | `() -> ()` | Append LF |
| `read_i32` | `(i32 line, i32 column) -> i32` | Consume one input line and return its integer |
| `discard_line` | `(i32 line, i32 column) -> ()` | Consume one input line |
| `fail` | `(i32 code, i32 line, i32 column) -> ()` | Throw a located runtime failure; must not return |
| `write_f64` | `(f64 value) -> ()` | Deterministic finite real output, preserving -0 |
| `read_f64` | `(i32 line, i32 column) -> f64` | Parse signed decimal/exponent input |
| `string_assign` | `(i32 destination, i32 capacity, i32 source, i32 line, i32 column) -> ()` | Copy a string value after capacity checks |
| `string_concat` | `(i32 destination, i32 capacity, i32 left, i32 right, i32 line, i32 column) -> ()` | Concatenate UTF-8 values |
| `string_compare` | `(i32 left, i32 right, i32 line, i32 column) -> i32` | Return -1/0/1 in scalar lexicographic order |
| `string_length` | `(i32 source, i32 line, i32 column) -> i32` | Count Unicode scalar values |
| `string_read` | `(i32 destination, i32 capacity, i32 line, i32 column) -> ()` | Read a whole input line atomically |
| `string_write` | `(i32 source, i32 line, i32 column) -> ()` | Append validated UTF-8 text |
| `string_index` | `(i32 source, i32 index, i32 line, i32 column) -> i32` | Read a 1-based scalar |
| `char_load` | `(i32 target, i32 line, i32 column) -> i32` | Read a char cell or tagged view |
| `char_store` | `(i32 target, i32 value, i32 line, i32 column) -> ()` | Write a char cell or view atomically |
| `write_char` | `(i32 value, i32 line, i32 column) -> ()` | Write one UTF-8 scalar |
| `read_char` | `(i32 line, i32 column) -> i32` | Read the first scalar of one input line |
| `string_char` | `(i32 destination, i32 capacity, i32 value, i32 line, i32 column) -> ()` | Widen a scalar to a string |
| `string_slice_size` | `(i32 source, i32 start, i32 count, i32 line, i32 column) -> i32` | Check scalar slice and return byte size |
| `string_slice` | `(i32 destination, i32 capacity, i32 source, i32 start, i32 count, i32 line, i32 column) -> ()` | Copy the checked scalar slice |

### Stream imports (indices 23–26)

All parameters are i32. Mode 0 is `read`; mode 1 is a `readln` field.

| Import | Signature | Behavior |
| --- | --- | --- |
| `stream_i32` | `(mode,line,column) -> i32` | Read checked integer token, retaining its delimiter |
| `stream_f64` | `(mode,line,column) -> f64` | Read checked finite real token |
| `stream_char` | `(mode,line,column) -> i32` | Read one Unicode scalar; mode 1 cannot consume LF |
| `stream_string` | `(destination,capacity,mode,line,column) -> ()` | Atomically read line remainder, retaining delimiter |

Numeric mode 0 skips line boundaries; mode 1 stays on the current line. String mode 0/1
both preserve the delimiter. `readln` emits field reads followed by one `discard_line`;
`read` emits only field reads. `write` omits the `newline` call. Line-reading legacy
imports still consume the remainder and delimiter themselves. After a value read
exhausts an unterminated final line, further value reads use `R_INPUT_EOF`; bare
discard_line may still consume that retained line. Blank terminated lines retain their
existing empty/invalid-input semantics.

### Formatted output (index 27)

`write_formatted(kind: i32, value: f64, width: i32, precision: i32, line: i32, column: i32) -> ()`
appends one complete padded field. Kind 0 is integer, 1 is real with the existing
formatter, 2 is Boolean, 3 is character, 4 is a string-object address, and 5 is real
with fixed decimal precision. All i32 values/addresses are exactly representable by
the f64 transport. Precision is zero unless kind is 5. Width counts Unicode scalars;
the output budget counts UTF-8 bytes, including padding. The host checks the complete
field before emission. The compiler snapshots string values and releases consumed
value borrows before evaluating width/precision, retaining their once-only order.
Use the bounds/rounding rules in the language reference. Invalid formatting reports
`R_FORMAT`; invalid memory, encoding, or real values retain their existing errors.
Legacy modules require no changes; newly generated modules need this matching import.

### Random integers (indices 28–29)

`randomize: () -> ()` reseeds private per-runtime state; `random_i32(bound: i32,
line: i32, column: i32) -> i32` draws a checked bounded integer. Invalid bounds use
`R_RANDOM_RANGE` without advancing state. Both imports are synchronous in batch and
JSPI hosts and provide no filesystem/network access. `limits.randomSeed` optionally
fixes initial and reseeding behavior for reproducible hosts/tests; values must be
unsigned 32-bit integers, with zero normalized to 1. Without it, the initial seed is 1
and randomize uses Math.random. The algorithm/ranges are in the language reference.
Legacy modules remain compatible with the updated host.

### Variant access (indices 30–32)

All parameters are i32. These memory-only services consume backend-owned immutable
schemas and frame-owned requests; they expose no filesystem or other host capability.

| Import | Signature | Behavior |
| --- | --- | --- |
| `variant_access` | `(request,writable,line,column) -> ()` | Check selected views; writable mode atomically activates them. Emitted code then links binding leases when required. |
| `variant_copy` | `(destination,source,size,capacity,request,line,column) -> ()` | Snapshot and validate the source, activation plan, controls, and live views before commit. Positive size is a whole fixed-size value; zero is a string with the given destination capacity. |
| `variant_read_string` | `(destination,capacity,mode,request,line,column) -> ()` | Validate line-remainder input before activating/storing its destination; stream cursor and mode rules remain unchanged. |

Only `variant_read_string` may suspend under JSPI. Synchronous batch hosts need no JSPI.
Requests and schemas use the private layouts below; legacy import signatures remain
unchanged, and older modules continue to work with the updated runtime.

## Runtime diagnostics

Locations use 1-based UTF-8 byte columns. The `fail` import must throw and never return.
The reference host maps generated numeric codes as follows:

| Number | Host diagnostic | Meaning |
| --- | --- | --- |
| 1 | `R_UNINITIALIZED` | uninitialized read |
| 2 | `R_OVERFLOW` | integer overflow |
| 3 | `R_DIV_ZERO` | division by zero |
| 4 | `R_DIV_DOMAIN` | restricted div/mod domain |
| 5 | `R_CALL_DEPTH` | routine call-depth limit |
| 6 | `R_RESULT` | function completed without a result |
| 7 | `R_MEMORY` | program storage exhaustion |
| 8 | `R_BOUNDS` | index/slice bounds |
| 9 | `R_FOR_CONTROL` | protected counted-control alias |
| 10 | `R_REAL_RANGE` | nonfinite real arithmetic |
| 11 | `R_CONVERSION` | real-to-integer range |
| 12 | `R_STRING_CAPACITY` | string capacity |
| 13 | `R_CHARACTER` | invalid Unicode scalar |
| 14 | `R_NIL` | nil access |
| 15 | `R_DANGLING` | dangling pointer |
| 16 | `R_DISPOSE` | invalid release |
| 17 | `R_LIFETIME` | live borrow |
| 18 | `R_POINTER` | malformed internal pointer |
| 19 | `R_RANGE` | value outside a declared subrange or enum domain |
| 20 | `R_SET_RANGE` | set constructor member or narrowing outside its domain |
| 21 | `R_VARIANT` | read through an incompatible active variant view |

Host input errors use `R_INPUT_EOF`, `R_INPUT_INVALID`, `R_INPUT_RANGE`, or
`R_ENCODING`. Input/output budgets use `R_INPUT_LIMIT`/`R_OUTPUT_LIMIT`; the session
watchdog uses `R_TIMEOUT`. Failed instances are discarded. Diagnostics/output are
rendered as text. Pascal operations must report located checked failures rather than
substituting raw Wasm bounds/conversion traps.

## Storage layouts and calling conventions

### Memory regions and scalar cells

The backend lays out 16-byte scalar cells (payload +0..7, initialized byte +8, reserved
protection byte +9, reserved metadata +10..15), aligned to 8 bytes. Types carry logical
structure only. Compound layout uses a 16-byte root followed by aligned children; all
counts, strides, and sums are bounded by 64 MiB. Invalid, recursive by-value, or
oversized layouts fail compilation with `E_LAYOUT`.

Integer, character, and enum subranges use the same 16-byte cells and base-scalar Wasm
signatures. The frontend retains constraints on storage and inserts explicit checks
for narrowing writes/calls/results; input and increment lowering also check before
committing. Var bindings preserve exact declared range identity. No new import is
needed; failure 19 reports the narrowing operation's location.

Enum payloads are zero-based i32 ordinals in 16-byte scalar cells. Their nominal
identity and the array index domain are frontend metadata. Array address arithmetic
uses the checked ordinal and declared lower bound; all domain kinds retain existing
nested layouts. Enum output looks up immutable string objects in a backend-emitted
member-name table shared by every output site for that enum. Tables/names count toward
the existing literal-data region; no runtime import or public export is added.

Sets use 48-byte objects: the ordinary 16-byte initialization header followed by
32 membership bytes. Bit zero represents the declared lower ordinal; all unused bits
are zero. Values/results/arguments use aggregate copy addresses and checked frame
snapshots. Reads require the set's initialization flag, and conversions construct an
independent result before commit. Membership outside the domain returns false. Set
operations are emitted directly into Wasm/WAT without additional host imports; failure
20 identifies invalid constructor endpoints or narrowing. Logical constant membership
bits do not contain target addresses or layout metadata.

Record `with` aliases retain an address and owning initialization root in private
locals. Scope floors preserve captured heap borrow records and temporary addresses
across statement cleanup and input suspension. Outward transfers restore exited
scope floors; loops within a binding retain its leases. There is no new storage object,
public export, or host import; ordinary lifetime/control/initialization guards apply.

Comma-separated array dimensions use these same nested objects and child headers;
index-list syntax introduces no flattened matrix representation or new host imports.

Addresses 0..262143 are reserved for decoded literals. Global objects follow, then a 32
KiB active-control address table and a 128 KiB frame arena. The remaining region is the
checked heap; these regions cannot overlap. Initial pages cover globals/frames, with
maximum 1024 pages. Private globals track call depth, frame top, control depth, heap
top, free-list head, and borrow head. Parameters, locals, and results are copied/cleared
in each fresh frame, including its 16-byte header reserved for lexical/lifetime
metadata. Allocation checks the frame end before any write and emits located failure 7
on exhaustion. Large activations may exhaust the frame sub-budget before reaching 64
calls. Main and all normal/early exits restore the prior frame top exactly once; failed
instances are discarded. Scalar loads check their own initialization byte, ready for
aliases. Failure 7 maps to `R_MEMORY` in the host and standalone exports.

### Arrays

Logical descriptors carry inclusive signed bounds and element identity. Backend size =
16 + count * element_size, including nested object headers/leaf flags. The checked
layout rejects overflow/excess before emission, even for unused named types. A selected
address = base + 16 + (index - lower) * stride is formed only after signed lower/upper
checks. Validated object sizes bound all arithmetic. Generated failure 8 maps to
`R_BOUNDS` at the selector's opening bracket.

Array copies move value and initialization bytes with forward/backward overlap ordering.
All padding/payload bytes are copied; protection/lifetime metadata belongs outside value
objects. Aggregate loads only supply copy addresses; they do not read scalar leaves.
Scalar loads check leaf flags. Assignment captures its destination before source
calls/input, preserving single evaluation.

### Records and aggregate calls

Fixed record fields use the same root header plus declaration-order child objects.
Offsets come only from checked backend sizes, independently of native C struct layout. Fields retain
per-leaf initialized bytes; whole/descendant
writes mark the result's root written, without initializing other leaves. Completion
with no result write still uses failure 6/`R_RESULT`.

Aggregate arguments are evaluated left-to-right into cleared private snapshots. Callee
parameters receive independent copies, preserving incomplete leaf state. An
aggregate-returning function has a hidden i32 destination parameter (after a static link
if present), followed by explicit parameters, and no Wasm return value. It copies its
result to the caller destination before frame release; scalar functions use i32/f64/i64
returns according to their type. The Pascal parameter limit still counts 64 explicit
parameters, not hidden ones. All snapshot/result buffers are checked leases in the
existing frame arena and produce located `R_MEMORY` on exhaustion. Argument/nested-call
leases are released on return; result leases survive until consumed. Statement/loop-test
cleanup prevents accumulation. These private copy addresses do not become Pascal
pointers; pointers use separate typed heap handles.

### Variant layouts, requests, and leases

A variant region follows its owner's fixed prefix. Its size is the largest arm body;
each body has a 16-byte header plus fixed fields and any nested region. Arms overlap at
the same address. Header +0 holds the one-based active storage arm (zero means none);
remaining header bytes are reserved. Tags are ordinary fixed fields and do not control
this header. Identical canonical field types at the same arm-relative offset share
storage; incompatible reads are rejected before interpreting payload or leaf flags.

The backend emits immutable little-endian schemas in the existing literal region.
A schema starts with arm count; each arm then has four u32 words: fixed-field count,
nested-region offset, nested-region size, and total body size. Each fixed field adds
three words: offset, size, and canonical type identity. IDs and offsets belong only to
the backend. Runtime caches are private to the attached instance and schemas must stay
immutable. Schema bytes count toward the 256 KiB literal region (`E_LAYOUT` on excess).

A frame request has four u32 header words: claim count (at most 128), current lease
head, control-table address, and active-control count. Each 16-byte claim has part
address, schema address, one-based selected arm, and fixed-field index; `0xffffffff`
requires the exact ancestor arm. Validation plans account for earlier ancestor clears
before reading nested headers. No plan mutates memory until all checks succeed.

Variant leases use the shared 16-byte frame chain: part address +0, previous lease +4,
schema address +8, kind 1 at +12. Heap leases retain kind 0. Leases are outside copied
values; whole-value replacement validates pinned headers/layouts before copying. Active
arm selection through a writable binding can clear incompatible views but does not set
leaf initialization. Module-private unpin handles both kinds, restoring only the
current expression/call/scope's leases. Programs with variants emit the ownership
helpers/globals even when they contain no Pascal pointer declarations.

### Reference bindings and counter guards

Each logical reference parameter consumes two private i32 arguments: actual object
address and owning root-written address. Its frame binding occupies 16 bytes, regardless
of pointee size; loads/selectors resolve through that binding. Binding never
reads/initializes the actual value; variant bindings first activate/pin their views
without marking leaves initialized. Writes update actual leaf state and the owning result
root. Forwarding passes the same pair. Value modes retain independent copies.

The backend reserves 8,192 control addresses (32 KiB) between globals and frames. Each
for loop protects its actual scalar cell; compiler-owned steps/cleanup bypass
source-write checks. Writes, reference bindings, and new counter initialization scan
active addresses against the destination object's bounded interval, emitting failure
9/`R_FOR_CONTROL` on overlap. This handles hidden/aggregate aliases without interpreting
payload bytes as flags. Static `E_FOR_CONTROL` checks remain in place. The frame header
stores entry control depth at +4; all exits restore it, while normal loop cleanup pops
one entry. Caller guards survive callee early returns. Memory/layout checks include this
metadata within 64 MiB.

The address table supplies active protection; cell byte +9 remains reserved.

### Static links and hidden arguments

Nested routines have an initial private i32 static-link parameter. An aggregate result's
hidden destination follows it, then explicit value/reference arguments. Frame header +0
stores the lexical parent's live frame; +4 saves protection depth. Calls to children
pass the current frame, siblings/recursive nested calls follow links to the callee's
parent, and captures follow links to their resolved owner. Globals retain absolute
storage. No global current-frame singleton is used.

A captured reference resolves its binding in the owner activation, then uses the same
actual/root descriptor. Captured result writes update their owning root.
Return/frame/call-depth and guard cleanup preserve the caller checkpoints.

### Real representation and formatting

Real payloads are eight bytes inside the same 16-byte cells; flags/guards and aggregate
layout are unchanged. Value parameters/results use f64; var bindings, links, and hidden
aggregate addresses remain i32. Typed signatures and scratch locals preserve
representation through recursion/captures/copies. Generated arithmetic rejects
signed-zero divisors and nonfinite results before use; narrowing rounds first and checks
before potentially trapping Wasm conversions.

Real I/O uses the write_f64/read_f64 signatures above. Failure codes 10/11 map to
`R_REAL_RANGE`/`R_CONVERSION`.

Real input and the deterministic decimal formatting algorithm follow the [language
reference](supported-language.md#real-numbers-and-conversions). Compiler literal parsing
uses bounded exact integer rationals, independent of locale; literal underflow to zero
is rejected. Numeric C hosts require IEEE binary64 double/default nearest rounding. C
constants store each intermediate at binary64 width; bitwise trunc/round avoid
locale/libm dependencies. Literal complexity limits are 768 significand digits and
exponent magnitude 4,096.

### String objects

A string object contains a 16-byte header followed by its padded byte capacity: byte
length u32 at +0, physical capacity u32 at +4, initialized u8 at +8. Declared capacities
are 0..1048576; a default string has 255 bytes. Object size is 16 +
round_up(capacity,16). Aliases/equal capacities share frontend identity; value
conversion checks actual length, while writable refs require exact identity. Zeroed
headers are uninitialized, including string leaves in partial aggregates.

String expressions travel as private object addresses. Read loads check flags;
argument/left-operand snapshots reserve actual length in checked frame leases. Declared
parameters/results reserve their full capacity, with a hidden destination for string
results as for other aggregates. Callee copies use string_assign with the destination
capacity rather than reading beyond an actual-length snapshot. Whole arrays/records copy
all bytes, including string payload/padding and flags. Counter protection remains
exclusively in its external table; cell +9 is reserved. Allocation lifetime metadata
likewise remains outside value objects.

Helpers validate memory/header bounds; writes preflight capacity/encoding, then copy
without partial mutation. Byte ordering of valid UTF-8 gives scalar ordering; length
counts scalars, not bytes/graphemes. NUL and BOM inside string values are preserved.
Invalid input surrogates use `R_ENCODING` rather than replacement. Existing
EOF/line/output limits apply. Helpers retain no persistent host string ownership or
allocator. See ABI compatibility below for host matching.

### Character cells and views

Char payloads are i32 Unicode scalars in ordinary 16-byte cells, with the existing
initialized flag at +8. Value parameters/results use i32. chr validates scalar range
before consumption; generated failure 13 maps to `R_CHARACTER`. Constants use
`E_CHARACTER`. String index/slice bounds use `R_BOUNDS` (8), or `E_BOUNDS` in constants.

A string-character path allocates a private 16-byte descriptor: string object address
u32 at +0, 1-based scalar index u32 at +4. The reference target sets bit 31 on that
descriptor address; normal addresses are below 64 MiB and untagged. char_load/store
distinguish cells/views, re-read the current string, and recheck scalar bounds after
aliases change lengths/byte widths. Replacement preflights capacity before mutation.
Root-written addresses retain the normal reference pair. Caller-owned leases keep
descriptors live through calls, forwards, and captures; consumed values/calls/loop
iterations release them. Views cannot escape as Pascal values or become integer counted
controls. Normal cells retain interval guards.

copy snapshots its source, then evaluates start/count. It queries checked UTF-8 byte
size before reserving a result lease and copying scalars. Char-to-string widening uses a
bounded four-byte capacity object. Existing frame/literal/memory limits and
failed-instance disposal apply. Matching hosts and standalone exports embed the same
fixed helpers without persistent host ownership.

### Pointer handles and heap leases

Pointer payloads/parameters/results use i64 handles: high generation u32, low payload
address u32; nil is zero. Logical type compatibility is frontend-owned; target
widths/layout remain backend-owned. Every constructed pointee layout, including
anonymous/unused types, is checked independently; pointer edges cut by-value recursion.
No address-of, arithmetic, integer cast, or pointer I/O exists.

Heap starts at the frame limit. A 16-byte-aligned block has a 32-byte header: free link
+0, capacity +4, generation +8, live +12, borrow count +16, actual size +20, reserved
+24/+28, then payload. Whole-block first-fit reuse increments the generation and clears
actual value bytes/flags. Otherwise checked bump allocation uses memory.grow, within 64
MiB. Fragmentation, growth failure, or generation wrap uses `R_MEMORY` before committing
the pointer. Header/lifetime metadata is outside copied values. New leaves are
uninitialized, including string/pointer leaves.

Dereference validates nil, address/alignment bounds, nonzero matching generation, live
state, and exact pointee size before returning a private address. Nil uses `R_NIL`,
stale/reused aliases `R_DANGLING`, malformed internal references `R_POINTER`. Dispose
validates with `R_DISPOSE`, checks active controls before borrowed lifetimes, then frees
and clears that pointer to nil; other aliases remain stale. No recursive free or GC
occurs. Equality compares tokens without dereferencing stale aliases.

Each dereference links a checked 16-byte borrow lease (allocation header +0, previous
lease +4, kind 0 at +12) in the shared frame arena and increments its block's borrow count. The frame
header saves entry borrow head at +8; module-private globals 3/4/5 track heap top, free
head, and borrow head. Ref bindings/character views retain caller borrows; value
arguments release their borrows after snapshot and before later arguments. Paths track
the latest pointee root, including returned pointers. Dispose of borrowed storage uses
`R_LIFETIME`; a counted control in the allocation uses `R_FOR_CONTROL` first.
Normal/early exits and expression/call cleanup unpin before restoring leases, preserving
caller state. Failed instances are discarded.

Allocator/validate/borrow/release/unpin functions stay private in generated Wasm. No
host imports or persistent JS ownership are added; matching ABI-v1 hosts only extend
failure-code mappings. Program exports remain exactly run/memory.

## Host input provider

`createPas2WasmRuntime(input, emit, limits, requestLine?)` retains its synchronous
three-argument batch API. An optional provider returns a Promise of one line without
CR/LF, or null for confirmed EOF. Only operations fetching a missing line may return
Promises; buffered operations remain synchronous. Parsing, storage, limits and cursor
ownership are shared between modes. Program output never includes host-side input echo.
JSPI wrapping is a host responsibility.

The reference implementation is [web/runtime/host.js](../web/runtime/host.js). Call
`attach(instance)` before `instance.exports.run()`; `emit(text)` receives program
output. `writeln` emits arguments in source order then one newline. The [language I/O
rules](supported-language.md#input-and-output) own numeric, Unicode, line, capacity, and
partial-assignment semantics.

## Worker protocol and sessions

Batch callers keep synchronous imports and call `instance.exports.run()` directly.
Interactive workers wrap input imports (legacy and stream helpers) with
`WebAssembly.Suspending` and await `WebAssembly.promising(instance.exports.run)()`. No
module transformation or additional Wasm exports are needed. Workers detect both APIs; unsupported live mode yields `E_JSPI`, with batch mode available.

The worker accepts `{type:'run',id,binary,input,interactive,limits?}` once per worker.
It emits capabilities (`jspi`), running, output (`text`), input-request
(`requestId,line,column`), done, or error (`diagnostic`) with the same run id. An
input-request accepts only a matching `{type:'input',id,requestId,text}` or
`{type:'end-input',id,requestId}`; EOF resolves the provider with null permanently. Each
line contains no CR/LF; submission contributes UTF-8 bytes plus one LF to the input
allowance. Empty lines are valid. Stale/duplicate messages are ignored. A probe message
returns capabilities without running a program.

The shared session owns cancellation, worker URLs, run/request identities and a
five-second cumulative active-time watchdog. It pauses on input-request and resumes on
delivery/running, never replenishing the budget. UI input echo is text-only and bounded
separately from program output. Console/session helpers are embedded with the
runtime/worker in exported HTML. The [product contract](spec.md#console-behavior) and
[language reference](supported-language.md#example-playground-console) define console
editing and UI behavior; these add no Wasm imports/exports or messages.

## ABI compatibility

Existing import names/signatures and numeric failure mappings remain available. String,
character, real, and stream helpers extend the v1 import set; newly emitted modules need
a matching host that implements their imports. Older v1 modules run with that extended
host, and old self-contained exports retain their embedded runtime. New exports embed
the matching repository implementation. Console input is a host capability, not a new
Pascal import or a binary transformation.

Compiler/API releases follow the separate [compatibility
policy](contributing.md#versioning-and-releases). Private backend addresses and hidden
calling conventions must not be exposed as Pascal pointers or host capabilities.
Production exports remain only `run` and `memory`.
