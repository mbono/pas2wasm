# Changelog

## Unreleased

### Added

- Optional `Pas2WasmConsole` component with per-run event adapters, independent widgets,
  inline line/key input, End input, configurable focus, scoped CSS variables and disposal.
  The playground and self-contained offline exports share the component; a two-console
  integration example and real-browser lifecycle/isolation tests exercise its public API.

- Headless `Pas2WasmEngine` browser API with cached static assets, serialized off-thread
  compilation, cancellation, one active run per engine, request-bound interactive input,
  raw output, text-file snapshots and explicit disposal. The playground uses the same API.
- Independent HTML embedding example and tested README/browser-embedding quick starts,
  nested-URL distribution checks and recorded performance/resource baselines.
- PR/main validation for document links, native/Wasm acceptance, sanitizers and independent
  Chrome embedding, separate from tag-triggered GitHub Pages deployment.

### Changed

- The DOM console controller lives in `runtime/console.js`; low-level UI/export
  integrations must load it alongside `console.js` and `runtime/files.js`. The headless
  session controller remains in `runtime/session.js`.

### Fixed

- Ubuntu CI tool setup now selects the SDK's Node executable explicitly, verifies and
  installs Binaryen 133 for linear WAT, and permits the downloaded Chrome sandbox through
  a dedicated AppArmor profile. Browser startup failures retain useful driver diagnostics.

## [0.5.0] - 2026-10-25

### Added

- Host-supplied UTF-8 input files and generated text-file downloads in the playground
  and offline HTML, with reusable input selections, exact result bytes, atomic
  validation and the shared 32-file / 1 MiB allowance. Compiler output is unchanged.

- Complete 23-file tutorial compatibility corpus pinned to original Git blob hashes,
  with shared expected input/output and native/Wasm/browser verification.

- Built-in partial `uses crt;` with directly emitted `readkey` helpers, checked Unicode
  scalars, synchronous batch input and live JSPI keys without automatic echo.
- Shared key/line typeahead with bounded paste/IME chunks, preserved editable prefixes,
  controls, EOF/accounting rules and matching offline HTML behavior.

- Per-execution virtual typed/text files with independent snapshots/cursors, bounded
  storage, checked owner/group lifetimes, and ordinary console/live/offline integration.
  Includes unchanged tutorial `files.pas` and CRT-backed `textfiles.pas`.

- Checked `@storage` for globals, locals/results, aliases, aggregate/heap fields and
  string characters, with stable typed identities, bounded registries, frame epochs,
  variant-view checks and unchanged tutorial `pointer_demo2` support.

- Nominal enumerations with member constants, comparisons, ordinals, checked
  increment/loops, subranges, case ranges, and formatted member-name output.
- Ordinal array domains for integers, characters, Booleans, and enumerations,
  including named/inline domains and the unchanged tutorial `enum_demo` source.
- Bounded ordinal sets with constructors/constants, union/difference/intersection,
  membership, subset/equality tests, checked domain conversion, and independent values.
  Includes the unchanged tutorial `set_demo` source.

- Record `with` scopes with once-captured paths, lexical field lookup, stable aliases,
  and heap lifetime/transfer cleanup across calls and interactive waits.

- Checked overlapping variant records with independent tags, compatible view aliases,
  nested arms, atomic activation/input, and pinned reference lifetimes. Includes the
  unchanged tutorial `rec` and `record_demo` sources.

### Compatibility changes

- `readkey` is now reserved. Rename existing declarations using that name. Only the
  documented CRT subset is available; other units and CRT routines remain unsupported.

- `file`, `assign`, `rewrite`, `reset`, `close`, `filesize`, `eof`, and `eoln` are now
  reserved. Rename existing declarations using those names; `text` remains shadowable.

- `with` is now reserved. Rename declarations previously using that name.
- `set` and `in` are now reserved. Rename declarations previously using those names.

### Fixed

- Render program-output Backspace as Unicode-scalar cursor movement and overwrite
  in the playground and offline HTML, preserving the waiting caret, raw output/file
  contents, input editing and cumulative byte allowances.

- Preserve variant storage when a write/input through a saved string-character
  pointer cannot retain an initialized string owner after activation.

## [0.4.0] - 2026-10-23

### Added

- Decimal Unicode character literals such as `#13`, `#27`, and `#128512`, with
  compile-time scalar validation and existing character/string operations.
- Integer and character subranges with checked storage, signatures, input, increment,
  and integer loop bounds, including the unchanged tutorial `subrange` source.

- Checked `inc(integer_storage)` with once-evaluated destinations, alias/control
  protection, and the unchanged tutorial multiplication-table example.
- Interleaved declaration sections and routines in global/local scopes, preserving
  source-order visibility and counted-control effects for globals declared later.
- All six Boolean comparisons in runtime and constant expressions, with `false < true`.
- Character/string comparisons in either order using Unicode lexical ordering and
  existing operand snapshots, including the unchanged tutorial `if_else` source.
- Output field widths and fixed real precision with bounded Unicode padding, exact
  binary64 rounding, and unchanged tutorial `formatted_io`/`io` examples.
- `randomize` and checked `random(n)` with per-runtime streams, reproducible host
  seeds, and the unchanged tutorial `vettori` source.
- Comma-separated multidimensional array bounds and indexes using existing checked
  nested-array storage, including the unchanged tutorial `vettori_multi` source.

### Compatibility changes

- `inc` is now reserved. Rename variables or routines previously named `inc`.
- `random` and `randomize` are now reserved; rename existing declarations using them.


## [0.3.1] - 2026-10-23

### Added

- MIT project license, source SPDX headers, and license notices in built sites
  and standalone HTML exports.
- GitHub Pages deployment of the example playground when a `v*` tag is pushed.

### Changed

- Consolidated overlapping specifications and historical plans into focused product,
  language/formal-grammar, architecture, interface, and contributor references.

- Consolidated project documentation under docs/ and reorganized the language
  reference with topic sections, tables, and formatted examples.

- README now focuses on building and using Pas2Wasm; developer workflows,
  architecture, verification, and hosting guidance moved to contributing.md.

## [0.3.0] - 2026-10-23

### Added

- `write` without a trailing newline, `read` preserving the remaining input,
  multiple read/readln arguments, and a shared checked input cursor. Existing
  supplied-input cases and synchronous batch hosts remain compatible.

- An interactive JSPI console sharing output and input in the playground and
  offline HTML exports, with Enter, End input, and Stop while waiting.
- A cumulative active-time watchdog excluding input waits, input echo limits,
  and explicit batch fallback when browser JSPI is unavailable.

- Terminal-style inline input directly after program prompts, with a native caret,
  Enter submission, protected history, Unicode editing, and plain-text paste.
- Interactive console is now the default in the playground and exported HTML;
  batch input remains selectable.

## [0.2.0] - 2026-10-22

### Added

- Underscores anywhere in identifiers, including leading underscores and a lone
  `_`, as an extension beyond ANSI/ISO 7185 Pascal.

- Typed pointers, nil, recursive pointees, checked allocation/reuse/disposal, and
  generation/lifetime guards across reference arguments and character views.

- Unicode char values, chr/ord, scalar indexing/updates, writable character views,
  and strict independent string slices, including constants and character I/O.

- Bounded UTF-8 string values, scalar-counting length, concatenation/comparisons,
  independent routine/aggregate copies, and whole-line input/output.

- Finite binary64 reals, exact decimal literals, numeric widening/conversions,
  typed routine/storage support, and deterministic real input/output.

- Lexically nested routines and forwards with recursive activation-safe captures.
- Cross-kind lexical shadowing and bounded per-scope declaration lookup.

- Scalar/aggregate `var` parameters sharing values and initialization through aliases.
- Runtime counted-control guards across calls, containing aggregates, and early returns.

- Fixed records, nested field lvalues, and independent array/record routine values.
- Left-to-right aggregate argument snapshots and bounded temporary result storage.

- Fixed/nested arrays with constant bounds, checked indexing, scalar element input,
  and value-copy assignments preserving partial initialization.

- Explicit typed lvalues and addressable linear-memory values/initialization flags.
- Checked backend layouts and fresh bounded frames with shared early-return cleanup.

- Procedure/function forwards and mutual recursion, with bounded call-graph effects.

- Typed function `exit(expression)` using existing early-return cleanup.

- Named scalar case labels and inclusive integer ranges with bounded overlap checks.

- Scalar type aliases with canonical frontend descriptors and `..` tokenization.

- Global/local integer and Boolean constants with checked declaration-time evaluation.

- A root specification separating completed milestones from planned language
  features, dependencies, design gates, and acceptance criteria.

- A single `VERSION` file embedded into native and Wasm compiler builds.
- `p2w_version()` and the JavaScript compiler's read-only `.version` property.
- Loaded compiler version in the playground heading and browser tab title.
- A documented semantic-versioning and release policy.

### Compatibility changes

- `type` is now reserved. Rename identifiers such as `var type:integer;` to
  `var kind:integer;` before upgrading. This requires a minor pre-1.0 release
  under the pre-1.0 compatibility policy.

- `forward` is now reserved. Rename old variables such as `forward` to `nextstep`.
  Forward declarations without bodies now report `E_FORWARD` instead of
  `E_UNSUPPORTED`. Previously rejected value exits, named case labels, and ranges
  now use the documented specific syntax/type/constant diagnostics where invalid.

- Activation storage now has an explicit 128 KiB sub-budget inside the unchanged
  64 MiB program-memory limit. Large-frame programs may report located `R_MEMORY`
  before the existing 64-call limit; small-frame baseline behavior is preserved.

- `record` is now reserved. Rename identifiers such as `record` to `entry` before
  upgrading under the existing pre-1.0 minor-release compatibility policy.

- M7a reserves 32 KiB of internal control metadata within the unchanged 64 MiB
  total memory limit; the activation arena remains 128 KiB. Forward-mode conflicts
  now use `E_FORWARD`; var parameter declarations are supported. Reference integer
  loop counters follow Pas2Wasm semantics and are excluded from FPC comparisons.

- `trunc` and `round` are now reserved; rename old identifiers to e.g. `rounded`.
  Real `/` and real declarations are supported. Generated modules add ABI-v1
  read_f64/write_f64 imports and need the matching extended host; older modules
  run with that host, and standalone exports continue embedding their own runtime.

- `length` is now reserved; rename old identifiers to e.g. `size`. `string` was
  already reserved. New modules require the matching ABI-v1 string helpers;
  existing modules/standalone exports retain their prior compatibility.
  String[0], byte capacities above 255, and inline string parameter/result types
  intentionally extend Free Pascal's compared subset.

- `char`, `chr`, `ord`, and `copy` are now reserved; rename existing identifiers.
  Char counts Unicode scalars, extending FPC's compared byte-character subset.
  New modules require the matching extended v1 character/slice helpers.

- `nil`, `new`, and `dispose` are now reserved; rename earlier identifiers.
  Dispose clears its pointer and rejects nil/stale/borrowed objects. Pointer
  equality compares generation tokens; these checked rules intentionally differ
  from unchecked Pascal implementations. Ownership remains in generated Wasm.
