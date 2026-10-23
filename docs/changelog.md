# Changelog

## Unreleased

### Added

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
