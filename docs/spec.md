# Pas2Wasm product specification

## Purpose and document responsibilities

Pas2Wasm is a portable C compiler with two required host builds: a native executable and
a WebAssembly compiler usable inside a browser. Both compile the same Pascal subset to
WebAssembly. The playground is an example integration; applications can embed the
compiler independently of that UI.

This specification owns product requirements, scope, and future design gates. Current
behavior is defined by the following maintained references:

| Document | Authority |
| --- | --- |
| [Language reference](supported-language.md) | Pascal semantics, diagnostics, restrictions, and the formal grammar appendix |
| [Compiler and runtime ABI](runtime-abi.md) | Interfaces, ownership, memory layouts, compiler limits, and worker messages |
| [Architecture](architecture.md) | Current implementation structure and design rationale |
| [Contributor guide](contributing.md) | Development workflow, full acceptance gate, hosting, and release policy |
| [Dependency inventory](dependencies.md) | Tool versions, purposes, licenses, and dependency restrictions |
| [Changelog](changelog.md) | Release changes and compatibility migrations |

The [VERSION file](../VERSION) is the single source for the compiler version. Historical
plans, design discussions, and verification records are available in Git history; they
do not override these current references. **Must** denotes a requirement. A **planned**
feature is not part of the supported language until its design gate is resolved,
implementation is accepted, and references are updated.

## Product invariants

| Area | Required contract |
| --- | --- |
| Compiler hosts | Maintain the native CLI and browser `pas2wasm.js` / `pas2wasm.wasm` from the same C core. Browser compilation is a primary capability. |
| Output backend | Only `wasm32` is implemented. Native host builds still emit Wasm; future CPU/OS output belongs behind the backend interface. Reject unsupported output targets explicitly. |
| Language | Accept ordinary documented Pascal without browser-specific `uses` clauses, annotations, or source rewriting. Document extensions and intentional dialect differences; do not claim full ISO 7185, Free Pascal, Turbo Pascal, or Delphi compatibility. |
| Backend boundary | Frontend/IR own logical types, resolved symbols/lvalues, semantics, and source locations. Wasm instructions, widths, imports, offsets, and calling conventions belong to the backend. Frontend/IR remain independently buildable/testable. |
| Integration | Source bytes/options enter a memory-only compiler API; artifacts identify target/format and have documented ownership. Files and the CLI are adapters. |
| Binary output | Emit a complete Wasm binary directly. Execution requires no assembler, linker, compiler service, or WAT conversion step. |
| WAT | Opt-in per compilation (`emit_wat` / `--emit-wat`), absent by default. Enabling WAT never changes binary bytes or behavior. Include the whole module and helpers with escaped data/names, UTF-8/LF, readable formatting, consistent indentation, and descriptive symbols where practical. Source annotations are optional. |
| Host parity | Identical source/options on the same compiler revision produce identical binary/WAT and compile diagnostics across hosts. Reassembled WAT preserves imports, exports, execution, and runtime diagnostics; reassembled bytes need not be identical. |
| Self-contained delivery | Ship repository-owned compiler/runtime code and plain HTML/CSS/JS with permitted toolchain support. No runtime service, account, API key, CDN, remote fonts, analytics, package installation, or dependency downloads. |
| Browser privacy | Source and program input remain in the browser. Fetch only assets from the same deployment; compilation/execution need no network after they load. |
| Browser isolation | Compiler and program run in terminable workers with separate memories. Stop/timeout must recover, and every Run uses fresh program/input/output state. |
| Checked execution | Preserve the language reference's evaluation, initialization, arithmetic, bounds, lifetime, and I/O rules. Limits and metadata remain inside the documented budgets; diagnose failures instead of relying on raw Wasm traps. |
| Capabilities | Programs receive bounded I/O, pseudorandom integers, and located failures, with no host/OS filesystem, network, DOM, or arbitrary-JavaScript capability. Virtual file I/O is confined to bounded per-execution storage. Render source-derived text safely. |
| Version identity | Embed VERSION at build time in both hosts. Display the loaded compiler version in the example heading/tab; no duplicated UI literal or runtime version-file fetch. Generated Pascal programs have no assigned compiler version. |
| Licensing | Repository-owned code/docs use MIT. Include the full license with site assets and inside standalone HTML; preserve third-party notices. |
| Maintainability | Keep code small, readable, documented, and free of speculative backend frameworks or unused dependencies. Follow contributor conventions. |

## Example browser integration and exports

The included playground demonstrates the compiler's integration requirements. These
contracts preserve its existing behavior without requiring every embedding application
to reproduce the same UI.

### Compilation, controls, and artifacts

- Provide a source editor, explicit interactive/batch selection, Run/Stop/Reset,
  compilation/runtime diagnostics, output, and a brief supported-language summary.
  Controls have readable labels and keyboard access; English is sufficient.
- Show loading, compiling, running, waiting, completed, stopped, and failed states.
  Run compiles current source and starts a fresh program only after success.
  Stop terminates compilation/execution/waiting and permits another run without
  reloading. Reset restores the example source/input and clears diagnostics/output.
- Keep Wasm, WAT, and HTML artifacts tied to current successful source. Editing
  source or failing compilation clears the WAT view and disables downloads;
  never execute or export a previous program after failure.
- Generate WAT is unchecked initially. Changing it clears previous WAT and disables
  its download. A successful WAT-enabled compile supplies a read-only view and
  `program.wat`; disabling it leaves these empty/disabled while Wasm and HTML
  downloads remain available after a successful compile. Viewing/downloading WAT
  works offline after assets load. Raw Wasm downloads remain available.
- Render diagnostics, WAT, and program output as text, never executable HTML.
  Stable diagnostic codes include understandable messages and 1-based UTF-8 byte
  locations. The compiler may stop after one accurate error; multiple-error
  recovery is not required. Runtime failures retain meaningful source locations.

### Console behavior

- Interactive input is the UI default in the playground and exported HTML; retain
  explicit batch input and synchronous batch hosts without JSPI. Both providers
  use identical parsing and program-output rules. Program output excludes UI echo.
- Use browser JSPI, capability-tested in the execution worker. Unsupported live
  mode reports `E_JSPI` and offers batch mode. Do not substitute prompt dialogs,
  program replay, or silently preloaded input. Suspend/resume the same instance,
  retaining its stack, frames, heap, cursor, and borrows.
- Flush prompts without LF before requesting input. An open empty queue waits;
  explicit End input closes the provider, preserving buffered data until consumed.
  Stop works while computing or waiting; stale run/request replies are ignored.
- Use one scrolling terminal surface with immutable history and an inline editable
  tail directly after prompts, with a visible caret while waiting. No separate
  input box or Send button. Keep Stop and End input outside transcript text.
- Enter commits/echoes a line once with one transcript LF. Empty lines are valid.
  Typing, paste, and IME confirmation do not submit. Support Unicode editing,
  Backspace/Delete, arrows, Home/End, and selection without changing history.
  Paste is plain text with line breaks normalized to spaces.
- Appended output preserves draft, caret, selection, and focus. Clicking focuses
  the active editor while permitting history selection/copying. Follow the tail
  when already at the bottom; permit scrolling back and restore the editor on
  the next request. Disable editing/caret outside input waits.
- Compilation has its own timeout. Execution uses a cumulative active-time budget:
  human waits are excluded, and each submission must not replenish it. Count UTF-8
  submissions and delimiters toward input; program output has a separate allowance,
  and UI echo uses the input allowance. Exact limits are in the language/ABI references.

### Standalone HTML

**Download runnable HTML** exports the exact current compiled program as a single
`program.html`, embedding the matching runtime, worker, UI/styles, and MIT notice. It
packages an already compiled program without requiring its source or compiler. Opening
raw `.wasm` alone is not an executable browser application.

The exported file must work after relocation to another directory/PC via `file://` in
supported desktop Chromium/Firefox with default security and a fresh profile, without a
prior playground visit, server, installation, cache, or network. Embed module bytes
safely (including text such as `</script>`); no sibling Wasm fetch, external
fonts/scripts, service workers, or runtime dependency downloads.

Execution waits for Run, starts with empty supplied input, uses a dedicated embedded
worker, and preserves both input modes, failures, limits, fresh runs, Stop/timeout, and
recovery. Share console/runtime helpers with the playground; release worker/Blob URL
resources. No SharedArrayBuffer, isolation headers, Asyncify production transformation,
or external terminal library is required.

## Implemented scope

All milestones below are complete; these identifiers explain existing commits, not
SemVer releases or a proposed implementation sequence.

| Milestone | Delivered capability |
| --- | --- |
| M0 | Shared compiler hosts, Wasm/optional WAT, worker integration, offline exports |
| M1 | Structured flow, procedures/functions, recursion, early exit, case selection |
| M2 | Embedded compiler identity and semantic versioning |
| M3 | Constants, type aliases, named/range labels, value exits |
| M4 | Forward declarations and mutual recursion |
| M5 | Typed lvalues, checked layouts, addressable storage, bounded frames |
| M6 | Fixed arrays/records and independent aggregate routine values |
| M7 | Writable references and lexical nested routines/captures |
| M8 | Finite binary64 arithmetic, conversions, and numeric I/O |
| M9 | Bounded UTF-8 strings, Unicode characters/views, and strict slices |
| M10 | Typed pointers, recursive types, checked allocation/reuse/disposal/borrows |
| M11 | Stream/multi-field I/O, JSPI console, inline terminal editing |

Feature completion requires the full [acceptance gate](contributing.md#acceptance-gate).
Current semantics and exact layouts belong to the references above, not separate
per-milestone contracts.

## Tutorial compatibility gates

Implement these increments separately and accept each through the full gate above.
The tutorial corpus is `mbono/pascal_tutorial` commit `a287978828a2d3a9af18aac2203c9aeb2b3d9d13`;
add unchanged source text only for examples resolved by the current increment.

| Increment | Resolved contract and acceptance |
| --- | --- |
| Checked increment | Reserve `inc`; accept only `inc(storage)` for writable integer storage. Evaluate its path once, read its initialized value, and add one with existing overflow and counted-control checks. Preserve heap borrows and owning-result flags. Reject constants, other types, nonstorage expressions, and extra arguments. Cover aliases, captures, side-effecting indexes, uninitialized/overflow failures, and unchanged `tabellina.pas`. No new host imports or ABI. |
| Flexible declarations | Permit repeated const/type/var/routine sections before the body in global and routine scopes. Bind declarations in source order; later names remain invisible to earlier routine bodies. Resolve pointer forwards within their own type section and routine forwards at scope end. Keep duplicate/shadowing/storage limits unchanged and bound global-write effects independently of declaration order. Accept unchanged `parametri.pas` and `subroutine.pas`, plus late globals, nested captures, forwards, and counted-control effects. No new host imports or ABI. |
| Boolean ordering | Accept all six comparisons between Boolean operands, with `false < true`, in runtime and constant expressions. Preserve eager left-to-right evaluation, initialization checks, distinct Boolean typing, and rejection of chained comparisons. Cover every operand pair and relation, aliases, side effects, and mixed-type rejection. `if_else.pas` remains pending its separate character/string comparison requirement. No new imports or ABI. |
| Character/string comparisons | Permit all six relations in either operand order by widening a character to a one-scalar string when the other operand is a string. Use existing Unicode lexical ordering and operand snapshots, including empty strings, non-ASCII text, constant folding, and alias-changing calls. Keep character/character ordinal comparisons and other type restrictions. Accept unchanged `if_else.pas` after Boolean ordering. No new imports or ABI. |
| Formatted output | Accept `value:width` for every printable type and `real_value:width:precision`, with integer formatting expressions. Evaluate value, width, and precision once left-to-right; snapshot strings and release consumed value borrows before formatting expressions. Width is a minimum Unicode-scalar count, right-aligned with spaces, never truncating. Allow width 0..1 MiB and precision 0..1000; invalid requests report located `R_FORMAT`, while output retains its cumulative byte limit. Explicit precision prints fixed decimals rounded from exact binary64 to nearest, ties to even, preserving negative zero; omitted precision retains the current real formatter. Append a formatted-write import without changing legacy imports. Cover extreme finite values, Unicode, snapshots/effects, invalid requests, limits, both browser modes/offline exports, and unchanged `formatted_io.pas`/`io.pas`. |
| Random integers | Reserve `randomize` and `random`; accept `randomize`/`randomize()` and `random(integer_bound)` for bounds 1..2147483647. Evaluate bounds once and return 0..bound-1 using per-runtime xorshift32 state with rejection sampling; invalid bounds report located `R_RANDOM_RANGE`. Initial seed is 1; randomize selects a nonzero seed with built-in Math.random. An optional host `limits.randomSeed` (u32, zero normalized to 1) fixes initial/reseed state for reproducible tests. Never evaluate randomness as a constant. Append two synchronous imports, preserve fresh runs and offline behavior, and make no cryptographic claim. Accept unchanged `vettori.pas` using a fixed test seed, plus range, independence, reset, evaluation, and UI/live/offline checks. |
| Multidimensional array syntax | Parse `array[l1..h1,l2..h2,...] of T` as nested nominal array constructors and `m[i,j,...]` as consecutive existing index selectors. Permit mixed bracket/comma selectors and ordinary subarray copies/references; every dimension keeps its checked bounds, source-order single evaluation, partial initialization, and alias/borrow rules. Nested array/string selectors retain their ordinary meaning. Attribute bounds failures to the relevant opening bracket or comma. Count each dimension toward existing depth/layout limits; reject reversed/noninteger/nonconstant bounds, excess indexes, malformed lists, and oversized layouts. Accept unchanged seeded `vettori_multi.pas`, matrix value/reference/heap interactions, both hosts, WAT and browsers/offline. No new runtime ABI or backend representation. |

## Remaining tutorial compatibility gates

These increments complete requirements 8–16 from the tutorial review. Resolve and
accept each independently; add each remaining original tutorial source only when its
dependencies are implemented. Preserve the full acceptance gate and separate commits.

| Increment | Resolved contract and acceptance |
| --- | --- |
| Numeric character literals | Accept decimal `#n` as a typed Unicode character constant, including NUL, CR, Escape, and supplementary scalars. Validate 0..1114111 excluding surrogates while lexing; malformed, negative, hexadecimal, or out-of-range spellings use located `E_CHARACTER`. Leading zeros are allowed. Use existing character folding, comparisons, concatenation with `+`, storage, and I/O; adjacent literal concatenation is not introduced. Cover constants, parameters/results, Unicode and invalid boundaries, both hosts, WAT, browsers, and offline exports. No new ABI. |
| Integer/character subranges | Accept named or inline constant `low..high` types with matching integer or character endpoints, including single-scalar quoted constants and `#n`. Reject reversed bounds with `E_RANGE`, incompatible endpoints with `E_TYPE`, and nonconstant endpoints with the existing constant/type diagnostics. Preserve distinct storage identities; values use the base scalar type. Check assignments, value parameters, function results/exits, input, and integer inc/for bounds before committing storage (`R_RANGE`, numeric failure 19). Ordinary arithmetic remains base-type arithmetic. Var arguments require identical declared storage types, including aliases, so references cannot bypass constraints. Keep existing 16-byte scalar cells and initialization/lifetime guards. Cover full integer endpoints, Unicode ranges, aliases, records/arrays/heap, captures/references, atomic failures, and unchanged `subrange.pas`. Ordinal array-domain generalization belongs to the enumeration increment. |
| Enumerations and ordinal array domains | Accept nominal `(first, second, ...)` types with at least one member, numbered from zero in declaration order. Members enter the ordinary lexical constant namespace and count toward the 4096-symbol budget; reject duplicate/reserved members and malformed lists. Aliases share identity; independent constructors do not. Permit all comparisons, constant folding, ord(enum), value/reference storage and results, enum subranges, enum for controls and checked inc (R_RANGE past the last member). Preserve counter protection and invalidation. Output uses original member spelling, including field widths; enum text input, integer casts, and arithmetic remain unsupported. Arrays accept matching constant integer/character/Boolean/enum bounds or a finite ordinal type domain, including comma-separated dimensions; retain domain identity and require compatible index values, checked bounds, existing initialization/copy/lifetime rules, and layout budgets. Enum case labels/ranges must match the selector's enum and retain overlap/reversal checks. Use logical ordinal metadata in frontend/IR and existing scalar cells; emit member-name data in the Wasm backend without new imports. Cover nominal mismatches, lexical visibility, inline routine types, aliases/captures/heap, evaluation order, boundaries/limits, both hosts/WAT/browsers/offline, and unchanged enum_demo.pas. |
| Bounded sets | Reserve set/in; accept set of an ordinal domain containing at most 256 consecutive ordinals, including subranges, enums, and Boolean. Set types are compatible by base identity; exact base/bounds define var-storage identity. Support empty/member/range constructors, constants, union (+), difference (-), intersection (*), membership (in), equality/inequality, and subset/superset (<=/>=); reject strict ordering and whole-set text I/O. Context binds constructor domains, including named empty constants; otherwise integer/character constructors default to 0..255 and enum constructors to their full domain. Typed binary operands use their enclosing domain (at most 256 ordinals); assignments/value calls/results may convert domains of the same base, rejecting present members outside the destination. Evaluate operands/endpoints once left-to-right, snapshot consumed sets across alias-changing calls, and validate constructor endpoints before writing; reversed valid ranges are empty. Compile-time invalid domain sizes/members use E_SET_RANGE; reversed type bounds retain E_RANGE; runtime constructors/narrowing use located R_SET_RANGE (failure 20), while out-of-domain membership is false. Sets have atomic initialization, independent copies, ordinary reference/capture/heap/lifetime protections, and bounded temporary storage. Frontend/IR retain logical domains/membership; the backend owns bitset cells and emitted helpers, with no new host imports. Accept unchanged set_demo.pas and cover empty/full/boundary sets, type/domain errors, constants, aliases, snapshots, limits, both hosts/WAT/browsers/live/offline. |
| Record with scopes | Reserve with; accept with record_storage {, record_storage} do statement for writable record paths, including indexed/nested records, references and returned-pointer dereferences. Capture each path once, left-to-right; later bindings can use earlier fields, and the rightmost/innermost matching field shadows ordinary lexical names. Unmatched names retain lexical lookup; bindings do not escape the body or enter called routines. Reading the record root is unnecessary; selected fields preserve initialization and ordinary type/reference/control guards. Fields may be assignment/input/inc/value/reference targets but cannot serve as simple for controls or constant case labels. Reject nonrecords and nonstorage temporaries with existing E_TYPE/E_LVALUE. Keep captured addresses/owning roots stable across calls, input waits and pointer/index mutation; pin heap owners for the scope and unwind pins/temporary storage on normal completion, break/continue/exit, cancellation and failure. Use backend-neutral binding identities in IR, bounded by existing nesting/node budgets; backend owns saved addresses and cleanup. No imports or public ABI changes. Cover shadowing, once-only paths, partial initialization, aliases/captures/results, transfers/loops, lifetime and memory reuse, both hosts/WAT/browsers/live/offline. The variant-record increment completes acceptance of record_demo.pas. |
| Checked variant records | Accept a final case [tag_name:] ordinal_type of labels:(fields) {; labels:(fields)} part, including nested variant parts, empty arms, optional trailing semicolons, named/anonymous tags, and constant ordinal label lists/ranges. Tags are ordinary independent fields: their values do not select/check arms. Reject duplicate field names across the record, overlapping/reversed/mistyped labels, and existing field/label/nesting/layout limits. Arms physically overlap. Fields at the same offset with identical declared type share values/initialization; writing one real view and reading its compatible real alias must work, regardless of the tag. A part tracks its current storage arm independently: initial reads use R_UNINITIALIZED; incompatible arm reads use located R_VARIANT (failure 21). Writes activate the needed arm, preserve exactly compatible fixed fields, clear incompatible incoming fields/nested parts, and commit only after all value/input/range/control/lifetime checks pass. Writable var/with/character bindings activate and pin their selected arm as a binding effect; incompatible arm changes or whole-value copies through a live pin use R_LIFETIME. Nested views pin their ancestors. Copies preserve arm/initialization state but never copy leases. Keep captured paths single-evaluated across calls, waits, aliases and transfers; include activation regions in counted-control checks. Frontend/IR own only logical parts/arms/fields/labels. Backend owns overlap layouts and bounded immutable schemas; narrow memory-only runtime helpers validate/read/commit atomically, using the existing frame lease chain without new external dependencies. Append imports without changing legacy signatures, and retain both synchronous and JSPI string input. Accept unchanged rec.pas (10.00, 20.00, 40.00) and record_demo.pas; cover all supported field types, nested/copy/reference/heap/control/snapshot interactions, atomic failures, budgets, both hosts/WAT/browsers/live/offline. |

### Address-of gate

Requirement 14 adds `@storage` for writable globals, locals/results, fields/elements,
references, heap subobjects, variant views, and stored-string characters. Capture paths
once without reading ordinary payloads; reject constants, routines and nonstorage
temporaries with `E_LVALUE`. Pointer types retain the declared storage constraint.
Taking a counter's address is allowed; writes through it retain `R_FOR_CONTROL`.

Keep legacy heap handles and allocation/disposal behavior. Address references use
opaque typed identities recording their owning root and global/frame/heap lifetime;
never expose raw addresses or permit casts/arithmetic. Frame epochs prevent reuse after
return (`R_DANGLING`), including escaped results and forwarded `var` aliases. Globals
last for the run; heap references retain allocation generation. `@p^` for a complete
heap object remains the original heap handle. Nonheap/interior disposal is `R_DISPOSE`.
Identical logical locations/lifetimes/types/views share identity; compatible variant
field aliases normalize by offset/type, while incompatible containing views stay
distinct. Canonical compatible views select a stable representative arm on later writes.

Address capture activates/pins selected variant views as a binding effect, but stored
pointers do not pin arms permanently. Consumed value arguments release capture borrows
before later arguments/callee execution, following existing snapshot rules. Dereferences carry their captured claims through
the existing read/deferred-write checks and temporarily borrow heap owners. Pointer
reads never activate an incompatible view. String-character references retain source
and scalar index, reconstruct transient descriptors, and recheck bounds after changes.
Preserve root-written flags, value/reference copies, input waits and transfer cleanup.

Frontend/IR retain only an address expression and its logical lvalue/type. Backend
owns tagged tokens, frame epochs and bounded access packets. Memory-only runtime
services track at most 65,536 live address identities (optional host `limits.references`
may lower this); reclaim stale identities without reusing IDs. Epoch/identity exhaustion
uses `R_MEMORY`. Append imports and keep old signatures. Accept unchanged
`pointer_demo2.pas`; cover all storage/type/view families, equality, stale/reused frames
and heap owners, nonheap/interior disposal, atomic failures, counters, limits, both
hosts/WAT/sanitizers/browsers, JSPI and offline exports. No new external dependency.

### Virtual-filesystem gate

Requirement 15 adds `file of T`, the built-in `text` type, `assign`, `rewrite`,
`reset`, `close`, `filesize`, `eof`, and `eoln`, plus explicit-file forms of existing
`read`/`readln`/`write`/`writeln`. Reserve `file` and these routine names. `text` remains
a shadowable type identifier, preserving existing variables, fields and aliases named
Text. Accept `file of T` for any copyable supported component, including constrained
ordinals, reals, strings, sets, aggregates/variants and typed pointers. Aliases preserve
identity; file types intern by declared component identity. `text` is distinct from
`file of char`.

File descriptors are noncopyable storage: permit standalone globals/locals, checked
pointer pointees and `var` aliases; reject descriptor assignment, value parameters,
function results, comparison, direct record/array components, and `file of file` with
`E_TYPE`. Pointer edges do not constitute by-value containment. Filename/ordinary value
expressions are evaluated once. Capture the file receiver once before other operands
and retain its owner through the whole multi-argument operation. Borrow the opened
descriptor binding for that group: nested I/O may use it, but close/reopen/reassignment
through an alias while it is in use fails with `R_FILE_STATE`.

| Operation | Contract |
| --- | --- |
| `assign(f,name)` | Bind an uninitialized/closed descriptor to a nonempty, NUL-free UTF-8 name of at most 255 bytes. Names are opaque, exact and case-sensitive; slashes provide no OS-path capability. Binding does not create contents. |
| `rewrite(f)` | Create/truncate named contents and establish their current kind/component identity; open for output. Reject an already open descriptor or contents with any live reader/writer. |
| `reset(f)` | Open existing compatible contents for input at the beginning. Multiple readers have independent cursors; any live writer excludes other opens. |
| `close(f)` | Release an open handle, preserving the descriptor's name and file contents for reopening in this execution. Reopening/reassigning an open descriptor or closing a closed one is an error. |
| `filesize(f)` | Require an open typed file; return its element count in either mode. |
| `eof(f)`, `eoln(f)` | Require input mode. EOF follows all elements/scalars and delimiters; EOLN is true at LF or EOF and accepts only text. |
| Typed `write(f,values...)` | Coerce/check each value against the declared component, snapshot it and append one independent element. Scalar values require initialization; aggregates preserve partial initialization/variant state as ordinary value copies do. |
| Typed `read(f,destinations...)` | Require exact component storage identity; copy the next element with ordinary control, root-written, variant and lifetime checks. Preserve character-view bounds/capacity checks. EOF never changes its destination. |
| Text I/O | Reuse console UTF-8 parsing/formatting and scalar destination families, with a separate cursor and no console input/output accounting. Output LF for writeln; readln discards the line remainder. Preserve Unicode, blank/unterminated lines, CRLF parsing, widths and precision. |

`readln`/`writeln` accept only text receivers. Typed read/write require at least one
value/destination and reject formatting; `writeln(t)` and `readln(t)` are valid.
Each field commits separately in source order, as console I/O does; a later failure
retains earlier successful fields. Per-field failures leave destination/file bytes and
activation state unchanged. Typed contents use backend-owned value snapshots including
initialization/arm state, not a portable external binary format. Pointer components
retain their ordinary lifetime checks; files do not extend a pointer owner's lifetime.

Every Run starts with an empty virtual filesystem. Bound it to 32 named files, 32
simultaneously open handles, 256 assigned descriptors and 1 MiB total contents; typed
snapshots count their full backend size. Preflight mutations and all budgets before
committing them. Reassignment reclaims a closed binding. Retire a returning activation's
own bindings/handles while preserving forwarded caller aliases; reclaim stale heap
bindings before enforcing resource limits. Closing/retirement preserves named contents.
Completion, failure, Stop and timeout release all contents, handles and bindings. No
persistence, browser storage, uploads/downloads, host filesystem or network access.

Use located `R_FILE_NAME`, `R_FILE_STATE`, `R_FILE_NOT_FOUND`, `R_FILE_TYPE`,
`R_FILE_EOF` and `R_FILE_LIMIT` for the respective name/state/existence/type/end/budget
failures. Unassigned descriptors retain `R_UNINITIALIZED`; text value parsing, encoding,
range and destination guards retain existing diagnostics. Unknown or unavailable file
operations remain compile-time errors.

Frontend/IR own logical file types/operations and single-evaluated receiver paths.
Backend owns descriptor layout, type IDs, materialization, epochs and narrow memory-only
imports. Runtime owns bounded per-execution contents/cursors; reuse current checked
copy/variant, formatting and input rules. File reads are synchronous with already present
contents, including JSPI hosts; console waits keep their existing behavior. No external
dependency or Pascal host capability is introduced. Passing examples include the
unchanged `files.pas`, `var` forwarding, typed aggregate/character snapshots and text
line roundtrips; reject `f:=g`, file-valued results, wrong components/modes, missing files
and exhausted limits. The unchanged `textfiles.pas` also requires requirement 16.
Accept through both hosts/WAT/sanitizers/actual browsers and relevant live/offline
exports, including atomic failures, ownership, cleanup, cancellation and fresh reruns;
update the language, ABI, architecture, dependency and contributor references before
marking the feature complete.

## Future scope and design gates

Virtual files use the contract above; partial CRT is the next tutorial increment. Other
entries below remain deferred without delivery promises; resolve each selected gate
before coding.

| Area | Decisions required before implementation |
| --- | --- |
| Same-routine `goto`/labels | Label visibility/definition, scope-entry restrictions, jump resolution, and structured lowering versus a dispatcher. Account for counted-control cleanup, result checks, frame lifetime, performance, and WAT readability. Cross-routine jumps remain outside an initial design. |
| Routine values / escaping closures | Indirect calls and environment ownership/lifetimes; ordinary static nested calls do not imply escaping closures. |
| Dynamic/open arrays, resizable strings | Runtime layout, allocation, bounds, copying, initialization, and lifetime rules. |
| Broader Pascal dialects | Chosen syntax, type identity, operations, conversions, and compatibility scope. |
| Native CPU/OS output | Instruction/object/executable formats, calling conventions, runtime services, and linking. Preserve the backend boundary without placeholder targets or a promised native ABI. |
| Broader I/O, WASI | Capabilities, host interfaces, portability, and offline behavior. |
| Partial CRT compatibility | Start with a documented subset and directly emitted helpers; extract a separate module if it grows. Browser services retain host imports. See the [planned architecture](architecture.md#planned-crt-compatibility); resolve raw-key input, JSPI, cancellation, and offline behavior before implementation. |
| Tooling and examples | Debug information, completion, shareable programs/tutorial links, localization, and a Pascal REPL. ANSI emulation/raw keypress input are separate from the current line console. |

Self-hosting, an optimizing/SSA IR, garbage collection, external frontend packages, and
dynamic backend plugins are not prerequisites. Add them only for an explicit
requirement. Before implementing a future feature, resolve its gate here with
syntax/semantics, representation boundaries, limits, diagnostics, and passing/failing
examples. Record durable rationale in architecture and current contracts in their owning
references; keep planned capabilities clearly marked until accepted.
