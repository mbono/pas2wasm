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
| Capabilities | Programs receive bounded I/O, pseudorandom integers, and located failures, with no host/OS filesystem, network, DOM, or arbitrary-JavaScript capability. Virtual file I/O remains planned below. Render source-derived text safely. |
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

## Future scope and design gates

The following are deferred, without effort or delivery promises:

| Area | Decisions required before implementation |
| --- | --- |
| Same-routine `goto`/labels | Label visibility/definition, scope-entry restrictions, jump resolution, and structured lowering versus a dispatcher. Account for counted-control cleanup, result checks, frame lifetime, performance, and WAT readability. Cross-routine jumps remain outside an initial design. |
| Routine values / escaping closures | Indirect calls and environment ownership/lifetimes; ordinary static nested calls do not imply escaping closures. |
| Dynamic/open arrays, resizable strings, variant records | Runtime layout, allocation, bounds, copying, initialization, and lifetime rules. |
| Sets, enumerations, subranges, `with`, broader Pascal dialects | Chosen syntax, type identity, operations, conversions, and compatibility scope. |
| Native CPU/OS output | Instruction/object/executable formats, calling conventions, runtime services, and linking. Preserve the backend boundary without placeholder targets or a promised native ABI. |
| Virtual files | Use bounded in-memory storage for one program execution: each Run starts empty, close preserves contents for reopening, and completion/failure/Stop/timeout releases everything. No host filesystem access or persistence across Runs/HTML sessions. See the [planned architecture](architecture.md#planned-virtual-filesystem); resolve types, operations, encoding, limits, and diagnostics before implementation. |
| Broader I/O, WASI | Capabilities, host interfaces, portability, and offline behavior. |
| Partial CRT compatibility | Start with a documented subset and directly emitted helpers; extract a separate module if it grows. Browser services retain host imports. See the [planned architecture](architecture.md#planned-crt-compatibility); resolve raw-key input, JSPI, cancellation, and offline behavior before implementation. |
| Tooling and examples | Debug information, completion, shareable programs/tutorial links, localization, and a Pascal REPL. ANSI emulation/raw keypress input are separate from the current line console. |

Self-hosting, an optimizing/SSA IR, garbage collection, external frontend packages, and
dynamic backend plugins are not prerequisites. Add them only for an explicit
requirement. Before implementing a future feature, resolve its gate here with
syntax/semantics, representation boundaries, limits, diagnostics, and passing/failing
examples. Record durable rationale in architecture and current contracts in their owning
references; keep planned capabilities clearly marked until accepted.
