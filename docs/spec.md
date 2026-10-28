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

## Headless browser embedding

`Pas2WasmEngine` is a small orchestration layer over the existing compiler
wrapper, compiler worker, runtime and execution controller. The resolved initial scope
is a headless API plus an independent HTML example and an optional console adapter.
The playground must consume the same engine. Native/C interfaces and Pascal semantics
remain unchanged. The API implements the following resolved contract.

| Area | Resolved contract |
| --- | --- |
| Delivery | One classic `engine.js` script bundles repository-owned headless helpers without a package manager. `create({assetBaseUrl})` resolves a directory relative to the document, fetches matching same-origin compiler loader/Wasm/bootstrap assets once, initializes a disposable probe worker, and reports the loaded `.version`. Initialization has a bounded timeout; no source/input is fetched or sent. |
| Compilation | `compile(source, {emitWat=false,target='wasm32',signal})` serializes requests, using a fresh compiler worker/memory per request and cached assets. The five-second timeout starts when a queued request begins. Results preserve binary/WAT/target/format and located compiler diagnostics. Abort/disposal reject with `AbortError`; infrastructure failures reject with `Pas2WasmEngineError` and `ENGINE_*` codes rather than Pascal diagnostics. Queued cancellation removes that request without delaying later work. |
| Execution | One active run per engine; overlap throws `ENGINE_BUSY`. Compilation may coexist with a run. `run(binary,{mode='batch',input='',files=[],onOutput,onInputRequest,onStateChange,onCapabilities})` returns a session before delivering callbacks. Each run owns fresh worker/memory/runtime/budgets/files. No caller buffers are detached. |
| Session | `.done` resolves once to `{status:'completed',files}`, `{status:'failed',diagnostic}` or `{status:'stopped'}`; disposal uses stopped. `.stop()` is idempotent. `.submit(value,requestId?)` returns whether a pending request accepted delivery; null is EOF, line text excludes CR/LF and includes any supplied prefix, key text is a nonempty raw chunk. Optional request IDs reject delayed answers to older requests; requests also expose a bound `submit(value)` for asynchronous applications. |
| Events | States are `running`, `waiting`, `completed`, `failed`, `stopped`. Input requests carry `requestId`, `kind`, `prefix`, `line`, `column`; output is raw text without UI echo. `onCapabilities({jspi})` reports worker support; unsupported interactive mode retains `E_JSPI` while batch works. Events precede done settlement; no events follow a terminal outcome. Throwing callbacks terminate the run with `ENGINE_CALLBACK` (zero location) and discard file results. Async callback returns are not awaited. |
| Ownership/limits | Preserve all existing limits, human-wait exclusions, generation/request guards and text-file byte/name ownership. Only UTF-8 text files are exchanged; Pascal typed files stay internal. A successful result owns its snapshots. Stop/failure/timeout yields no file results. |
| Disposal | Stop active sessions, abort queued/active compiles, clear timers/listeners, terminate workers, release Blob URLs/cached assets. Repeated disposal is harmless. Later compile rejects and run throws `ENGINE_DISPOSED`. |
| Compatibility | Ship compiler/runtime/engine assets from the same release together. Keep the `pas2wasm_v1` ABI and all expected diagnostic codes/locations. Offline exports embed only their existing program/runtime/UI functions and make no sibling requests. Blob workers require suitable CSP; static-worker/CSP optimization and warm compiler reuse are follow-ups requiring measurements. |

Acceptance includes independent subpath-hosted HTML and exercised README/guide examples,
both actual browsers, line/key/EOF/prefix input, diagnostics/WAT parity, callback and
worker failures, queued/active abort, overlapping runs, stop/dispose/recovery, bounded
file ownership, resource release/stale replies, offline reruns and existing exports.
The browser suite records cold/repeated compile and startup timings, asset sizes and
retained workers before optimization. Change-time CI is separate from the existing
tag-only Pages deployment.

## Reusable console

The component is `Pas2WasmConsole.create(container,{autoFocus:true})`, shipped
in a separate classic `console.js` bundle. It appends owned light DOM containing only
the terminal. Applications own engines, Run/Stop, modes, batch input, diagnostics
and files. It needs no engine, fixed DOM IDs, framework or fetches.

`createHandlers()` resets output/draft state and returns `onOutput`, `onInputRequest`
and `onStateChange`. Each connection invalidates older callbacks, including after
terminal states. Applications stop previous sessions before replacing connections.
Requests use their bound submit method; preserve line echo, raw keys, prefetched
prefixes, limits and native editing. The component has no EOF control, public EOF
method or Ctrl+D shortcut. Programmatic EOF remains available through the headless
engine's existing null submission. `clear()` clears output while preserving a pending
request/draft; `focus()` focuses an active editor without losing its selection.
`autoFocus:false` suppresses request-driven focus but retains explicit/user focus.

`dispose()` is idempotent, invalidates callbacks and removes owned DOM/styles/listeners;
it never stops or disposes an application-owned session/engine. Other public calls after
disposal throw InvalidStateError; delayed callbacks become harmless. Multiple consoles
must remain independent. Styles are scoped and use `--p2w-console-background`, `-color`,
`-caret-color`, `-font`, `-min-height` and `-max-height` variables with current defaults.

The playground and compiler-free offline exports must share this component. Accept
two-instance isolation, stale/repeated bindings, cleanup, focus/clear during waits,
trusted line/key/paste/IME input, native selection/caret, style customization,
batch/JSPI fallback and fresh offline exports through the full acceptance gate. Update
owning references and static allowlists.

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
- Flush prompts without LF before requesting input. An open empty queue waits.
  The shipped UIs provide no manual EOF action; programs requesting more input wait
  for delivery or Stop. Headless EOF preserves buffered data until consumed.
  Stop works while computing or waiting; stale run/request replies are ignored.
- Use one scrolling terminal surface with history protected from user editing
  and inline input at the output cursor, with a visible caret while waiting.
  No separate input box or Send button. Keep application-owned Stop outside transcript text.
- In line mode, Enter commits/echoes a line once with one transcript LF. Empty lines are valid.
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

### Backspace output display

The shared playground/export renderer interprets program-output Backspace (`#8`) as
moving its output cursor left by one Unicode scalar on the current logical line,
clamped at that line's start. It does not erase. Subsequent output overwrites existing
scalars before extending the line: `ab`, Backspace, `C` displays `aC`; `abc`, Backspace
alone still displays `abc` with the waiting caret before `c`. The ordinary
Backspace-space-Backspace sequence erases the previous scalar visibly and leaves the
cursor at that position.
LF preserves the rest of the current line and starts a new line at column zero.
Browser soft wrapping does not create logical lines. Combining marks and wide/emoji
characters retain scalar semantics; grapheme clusters and terminal-cell widths are not
introduced. Other output controls retain their current behavior; no ANSI parser is added.

Interpret output consistently across chunk boundaries, both input modes, both browsers
and standalone offline HTML. Preserve text-only rendering, read-only output, native
draft editing, composition, selection/focus, scrolling, Stop/recovery and fresh runs.
The input caret sits at the output cursor while waiting, including before unoverwritten
text. Line-submit echo remains literal input followed by LF; user Backspace edits only
the draft in line mode and remains `#8` input in raw-key mode.

This is display behavior only: compiler artifacts, raw runtime/worker output and virtual
file bytes remain unchanged. All emitted bytes, including Backspaces and overwritten
characters, still consume the existing cumulative output allowance; no budget is refunded.
Process bounded chunks without per-character whole-transcript DOM replacements. Cover
chunk splitting, Unicode, line/empty/start boundaries, overwrite/erase sequences, caret
geometry, copy/history and draft preservation, raw tutorial file replay, limits and
offline exports through the full acceptance gate before completion.

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

### Text-file input and download gate

The host may supply UTF-8 text files before execution and retrieve generated text files
after success. This adds runtime/worker/UI interfaces only: Pascal syntax, the C
compiler, generated Wasm/import signatures and typed-file representation stay unchanged.
Pascal still has no host/OS filesystem access. File selection and downloads are explicit
user actions; there is no network transfer, persistence or automatic download.

| Area | Resolved contract |
| --- | --- |
| Inputs | A Files panel accepts multiple local files by their exact case-sensitive basename. Validate all candidate names/counts/sizes before reading bytes, then strict UTF-8 before accepting the entire batch. Empty files, BOM, NUL and CRLF contents are preserved byte-for-byte. Reject duplicate names, including existing selections, rather than replacing them silently. Each input can be removed; Clear inputs removes all. |
| Limits | Uploaded and generated virtual files share the existing 32-name / 1 MiB total-content budget. Each input is at most 1 MiB; names require 1..255 UTF-8 bytes without NUL or invalid Unicode. Display selected counts/bytes and the remaining allowance. Input contents do not consume console input/output allowances. |
| Runs | Retain validated input selections only in page memory until removed/Reset/reload. Every Run receives fresh copies, including after a previous run rewrote an uploaded name. `assign` plus `reset` reads a supplied file synchronously; missing/type/mode checks and per-run budgets retain existing diagnostics. Invalid selection batches preserve previous accepted inputs and never seed partial contents. |
| Results | After successful completion, list names/sizes of files last rewritten as text, including empty files and rewritten input names. Unchanged inputs and typed snapshots are not generated text results. Capture exact bytes before `file_finish` cleanup; a result is a bounded, host-owned snapshot, not a surviving live filesystem. Downloads preserve bytes, including control characters. |
| Failure/cancellation | Publish no results on compile/runtime errors, Stop, timeout or stale messages. Clear previous results on a new Run, source/file-selection changes, Reset or input-mode cancellation. Disable file-selection changes while compiling/running/waiting. Pending file reads disable Run and use a generation guard; Reset discards stale reads. |
| Rendering/download | Render filenames/status as text, never markup. Download only on a user's click; replace path separators, control characters and nonportable filename punctuation, and trim leading/trailing dots/spaces in the suggested disk name, retaining the original virtual name in the list. Release temporary Blob URLs. |
| Offline exports | Embed the same Files controller in standalone HTML. It starts with no selected inputs/results, supports selection/download in both input modes under fresh offline `file://` profiles, and never embeds the playground's selected file bytes. |

The runtime host API adds `loadFiles([{name,data:Uint8Array}])` after fresh attachment
and before the entry call, plus `takeFiles()` to transfer generated snapshots after
success. Loading validates/copies all entries before installing any, and consumes its
one fresh-load opportunity even for an empty list. `file_finish` captures generated text
then clears live bindings/contents; failures, attachment and `finish()` discard pending
results. Taking results consumes them. Existing callers without files retain their
behavior. No additional Wasm global/import/export or runtime dependency is required.

Worker Run messages accept optional `files`, and successful done messages include the
final `files` snapshots; omitted lists default to empty. Deliver results atomically with
done, behind the existing run identity guard, before disposal. Never publish provisional
results or retain failed worker state. Expose these host interfaces independently of
the example UI. Reuse existing `R_FILE_NAME`, `R_FILE_LIMIT`, `R_FILE_STATE`,
`R_FILE_TYPE` and `R_ENCODING` errors for invalid host file lists/names/budgets/encoding.

Accept exact/over-limit counts and bytes, invalid/duplicate names and encoding, atomic
selection/loading, immutable input copies, rewrite/truncate and generated-only results,
empty/BOM/Unicode/CRLF/control bytes, name safety, fresh runs, stale reads/messages,
failure/cancellation cleanup, real uploads/downloads and matching offline behavior in
Chromium/Firefox. Preserve all existing cases and run the full acceptance gate, update
owning references, and commit the completed feature separately.

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

The 16 tutorial increments are implemented separately under the full acceptance gate.
The complete corpus is `mbono/pascal_tutorial` commit
`a287978828a2d3a9af18aac2203c9aeb2b3d9d13`: all 23 unchanged source files are pinned
by Git blob identity in [the test manifest](../test/tutorials.json), with shared input
and expected output. Their resolved contracts follow; deferred capabilities remain
in [Future scope](#future-scope-and-design-gates).

| Increment | Resolved contract and acceptance |
| --- | --- |
| Checked increment | Reserve `inc`; accept only `inc(storage)` for writable integer storage. Evaluate its path once, read its initialized value, and add one with existing overflow and counted-control checks. Preserve heap borrows and owning-result flags. Reject constants, other types, nonstorage expressions, and extra arguments. Cover aliases, captures, side-effecting indexes, uninitialized/overflow failures, and unchanged `tabellina.pas`. No new host imports or ABI. |
| Flexible declarations | Permit repeated const/type/var/routine sections before the body in global and routine scopes. Bind declarations in source order; later names remain invisible to earlier routine bodies. Resolve pointer forwards within their own type section and routine forwards at scope end. Keep duplicate/shadowing/storage limits unchanged and bound global-write effects independently of declaration order. Accept unchanged `parametri.pas` and `subroutine.pas`, plus late globals, nested captures, forwards, and counted-control effects. No new host imports or ABI. |
| Boolean ordering | Accept all six comparisons between Boolean operands, with `false < true`, in runtime and constant expressions. Preserve eager left-to-right evaluation, initialization checks, distinct Boolean typing, and rejection of chained comparisons. Cover every operand pair and relation, aliases, side effects, and mixed-type rejection. `if_else.pas` remains pending its separate character/string comparison requirement. No new imports or ABI. |
| Character/string comparisons | Permit all six relations in either operand order by widening a character to a one-scalar string when the other operand is a string. Use existing Unicode lexical ordering and operand snapshots, including empty strings, non-ASCII text, constant folding, and alias-changing calls. Keep character/character ordinal comparisons and other type restrictions. Accept unchanged `if_else.pas` after Boolean ordering. No new imports or ABI. |
| Formatted output | Accept `value:width` for every printable type and `real_value:width:precision`, with integer formatting expressions. Evaluate value, width, and precision once left-to-right; snapshot strings and release consumed value borrows before formatting expressions. Width is a minimum Unicode-scalar count, right-aligned with spaces, never truncating. Allow width 0..1 MiB and precision 0..1000; invalid requests report located `R_FORMAT`, while output retains its cumulative byte limit. Explicit precision prints fixed decimals rounded from exact binary64 to nearest, ties to even, preserving negative zero; omitted precision retains the current real formatter. Append a formatted-write import without changing legacy imports. Cover extreme finite values, Unicode, snapshots/effects, invalid requests, limits, both browser modes/offline exports, and unchanged `formatted_io.pas`/`io.pas`. |
| Random integers | Reserve `randomize` and `random`; accept `randomize`/`randomize()` and `random(integer_bound)` for bounds 1..2147483647. Evaluate bounds once and return 0..bound-1 using per-runtime xorshift32 state with rejection sampling; invalid bounds report located `R_RANDOM_RANGE`. Initial seed is 1; randomize selects a nonzero seed with built-in Math.random. An optional host `limits.randomSeed` (u32, zero normalized to 1) fixes initial/reseed state for reproducible tests. Never evaluate randomness as a constant. Append two synchronous imports, preserve fresh runs and offline behavior, and make no cryptographic claim. Accept unchanged `vettori.pas` using a fixed test seed, plus range, independence, reset, evaluation, and UI/live/offline checks. |
| Multidimensional array syntax | Parse `array[l1..h1,l2..h2,...] of T` as nested nominal array constructors and `m[i,j,...]` as consecutive existing index selectors. Permit mixed bracket/comma selectors and ordinary subarray copies/references; every dimension keeps its checked bounds, source-order single evaluation, partial initialization, and alias/borrow rules. Nested array/string selectors retain their ordinary meaning. Attribute bounds failures to the relevant opening bracket or comma. Count each dimension toward existing depth/layout limits; reject reversed/noninteger/nonconstant bounds, excess indexes, malformed lists, and oversized layouts. Accept unchanged seeded `vettori_multi.pas`, matrix value/reference/heap interactions, both hosts, WAT and browsers/offline. No new runtime ABI or backend representation. |

## Additional tutorial compatibility contracts

Requirements 8–16 cover the character, type, storage and I/O increments below. Each
retains its independently resolved contract and full acceptance gate; current language
and ABI details remain in their owning references.

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

Every Run starts with an empty virtual filesystem or explicit host-supplied text inputs
under the [text-file gate](#text-file-input-and-download-gate). Bound it to 32 named files, 32
simultaneously open handles, 256 assigned descriptors and 1 MiB total contents; typed
snapshots count their full backend size. Preflight mutations and all budgets before
committing them. Reassignment reclaims a closed binding. Retire a returning activation's
own bindings/handles while preserving forwarded caller aliases; reclaim stale heap
bindings before enforcing resource limits. Closing/retirement preserves named contents.
Completion, failure, Stop and timeout release all live contents, handles and bindings.
Successful text results may be copied to the host before cleanup. No persistence,
browser storage, direct host filesystem or network access is provided.

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
and exhausted limits. The unchanged `textfiles.pas` combines this capability with requirement 16.
Accept through both hosts/WAT/sanitizers/actual browsers and relevant live/offline
exports, including atomic failures, ownership, cleanup, cancellation and fresh reruns;
update the language, ABI, architecture, dependency and contributor references before
marking the feature complete.

### Partial CRT and raw-key gate

Requirement 16 accepts `uses crt;` immediately after the program header, before
ordinary declarations. Unit spelling is case-insensitive; unknown units use `E_UNIT`
and duplicate CRT entries use `E_DUPLICATE`. This built-in compatibility unit exposes
only `readkey`/`readkey()`, returning one Unicode `char`. Calling it without CRT uses
`E_UNIT`; arguments use `E_ARGUMENT`; constant evaluation uses `E_CONST`. Reserve
`readkey` and retain `uses` as reserved; `crt` itself remains an ordinary identifier
outside the unit clause. Unknown CRT routines remain ordinary compile-time errors.

Emit the CRT wrapper directly in generated Wasm and optional WAT, calling a narrow
memory-only keyboard import and validating its returned Unicode scalar. Do not add a
precompiled CRT module, external terminal library, WASI or dependency. Frontend/IR own
only the unit/call semantics; backend owns helper/import details. Native and Wasm hosts
must retain binary/WAT parity, opt-in WAT and unchanged production exports.

| Input source | Required behavior |
| --- | --- |
| Batch | Use the existing supplied-input cursor synchronously without JSPI. Preserve remaining buffered scalars and map LF/CRLF line delimiters to Enter (13). Literal CR and Escape are supported. Exhaustion uses `R_INPUT_EOF`. |
| Interactive key | Suspend the same instance with JSPI only when no buffered input remains. Return printable Unicode scalars, Enter 13, Escape 27, Backspace 8, Tab 9 or Delete 127. Ignore unsupported navigation/modifier keys while protecting transcript history; Shift+Tab retains normal focus navigation. |
| Paste/composition | Commit a bounded raw-key chunk once, normalize CRLF/LF to CR and consume its scalars in order. Raw-key input has no automatic echo: the Pascal program decides what to print. Native IME preedit is permitted; confirmation must not add a second character or Enter. |
| Mixed line/key reads | Share the logical console cursor. Keys consume existing line remainder/delimiters first. Complete CR-delimited raw chunks may satisfy later line reads without waiting. An unterminated queued suffix becomes the next line editor's prefix, preserving all accepted input. |

Line-mode editing/paste/Enter and existing batch fixtures retain their current behavior.
Raw-key paste is a key stream, so its queued CR may complete a later line; line-mode
paste still normalizes breaks to spaces and never submits. When an accepted raw suffix
becomes a line prefix, Enter commits the edited line with ordinary UI echo. A headless
null submission instead delivers only the already accepted suffix as an unterminated
final line, without submitting or echoing editor drafts. Empty queues at programmatic
EOF use ordinary EOF. Typed keys/committed compositions remain inert text; no ANSI
emulation, arbitrary key hooks, DOM or JavaScript capability is granted.

Input requests distinguish `line` and `key` (legacy requests default to line), retaining
run/request IDs and stale-reply rejection. The optional host provider gains backward
compatible kind/prefix arguments. Key responses may contain multiple scalars, bounded
by the cumulative 1 MiB input budget. Charge normalized key chunks once without an
extra delimiter; charge line submissions/delimiters while crediting an already charged
prefix once. Queue storage stays inside that allowance. Buffered processing consumes
active execution time; only actual human waits pause the existing cumulative budget.
A virtual-file cursor never consumes raw console input or requests a human key/line.

Keep one console with an inline visible caret while waiting, protected history, focus,
selection and normal copy behavior. Do not echo raw keys or create a separate keyboard
input control. Stop, source/mode changes, errors, timeouts and new Runs must clean up
input queues/workers and preserve recovery. Both input modes and all behavior
must also work in standalone offline `file://` exports in Chromium and Firefox.

Accept unchanged `textfiles.pas`, plus key/line mixing, Unicode/controls, chunking,
IME/paste, EOF, input limits, lifetime/counter/variant/file interactions, unsupported
JSPI fallback, trusted browser Enter/Escape, caret visual review, cancellation and
active-budget checks. Preserve all existing batch/line cases. Run the full acceptance
gate and update the language/grammar, ABI, architecture, dependencies, contributor
references and changelog. FPC CRT reads use different terminal services; compare only
where semantics agree. Finally verify all 23 unchanged tutorial sources from the pinned
revision with appropriate input and expected output before completing the full goal.

## Future scope and design gates

The tutorial increments use the resolved contracts above. Other entries below remain
deferred without delivery promises; resolve each selected gate before coding.

| Area | Decisions required before implementation |
| --- | --- |
| Same-routine `goto`/labels | Label visibility/definition, scope-entry restrictions, jump resolution, and structured lowering versus a dispatcher. Account for counted-control cleanup, result checks, frame lifetime, performance, and WAT readability. Cross-routine jumps remain outside an initial design. |
| Routine values / escaping closures | Indirect calls and environment ownership/lifetimes; ordinary static nested calls do not imply escaping closures. |
| Dynamic/open arrays, resizable strings | Runtime layout, allocation, bounds, copying, initialization, and lifetime rules. |
| Broader Pascal dialects | Chosen syntax, type identity, operations, conversions, and compatibility scope. |
| Native CPU/OS output | Instruction/object/executable formats, calling conventions, runtime services, and linking. Preserve the backend boundary without placeholder targets or a promised native ABI. |
| Broader I/O, WASI | Capabilities, host interfaces, portability, and offline behavior. |
| Tooling and examples | Debug information, completion, shareable programs/tutorial links, localization, and a Pascal REPL. ANSI emulation and broader terminal protocols remain separate from the documented key/line console. |

Self-hosting, an optimizing/SSA IR, garbage collection, external frontend packages, and
dynamic backend plugins are not prerequisites. Add them only for an explicit
requirement. Before implementing a future feature, resolve its gate here with
syntax/semantics, representation boundaries, limits, diagnostics, and passing/failing
examples. Record durable rationale in architecture and current contracts in their owning
references; keep planned capabilities clearly marked until accepted.
