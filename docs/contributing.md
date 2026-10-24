# Contributing to Pas2Wasm

Start with [spec.md](spec.md) for product requirements, implemented scope, and future
design gates. Current contracts belong to the [language reference and formal
grammar](supported-language.md) and [compiler/runtime ABI](runtime-abi.md).
[Architecture](architecture.md) explains implementation choices.
[README.md](../README.md) covers building and using the compiler; the [dependency
inventory](dependencies.md) lists tool versions/licenses.

## Project layout and development builds

| Path | Responsibility |
| --- | --- |
| `src/frontend/`, `src/ir/` | Target-independent analysis, types, storage paths, and typed nodes |
| `src/backend/wasm/` | Layouts, direct binary/optional WAT, and checked heap helpers |
| `src/driver/`, `src/pas2wasm.h` | Native CLI adapter and memory-only compiler API |
| `web/` | Example playground, workers, runtime, and HTML exporter |
| `test/` | Shared fixtures, independent helper tests, and acceptance runners |
| `docs/` | Current requirements, language/API references, architecture, and development guidance |
| `build/` | Ignored generated artifacts/screenshots; do not hand-edit or publish wholesale |

GNU Make 4.3+, a C11 compiler, and Emscripten build both hosts. Python 3 serves the
example site. On Arch, the login profile normally adds Emscripten to PATH; if `emcc` is
unavailable, source `/etc/profile.d/emscripten.sh` or pass
`EMCC=/usr/lib/emscripten/emcc` to Make. See README for build/run targets.

Override `CC`, `EMCC`, `CFLAGS`, `CPPFLAGS`, or `LDFLAGS` when needed; clean before
rebuilding with changed flags. Warnings are enabled for both hosts. Production builds
need no JavaScript packages, bundler, WAT assembler, or comparison Pascal compiler. Once
toolchains/SDK components are installed, ordinary builds work offline.

## Tests and fixtures

| Command | Coverage | Additional prerequisites |
| --- | --- | --- |
| `make test` | Frontend/layout/numeric helpers, native/Wasm binary/WAT/diagnostic parity, execution, WAT reassembly, compatible FPC comparisons | Node.js, Binaryen `wasm-as`, Free Pascal |
| `make test-sanitize` | Instrumented native acceptance and layout/numeric helper checks | Same tools and C address/undefined-behavior sanitizers |
| `make test-browser` | Actual Chromium/Firefox compiler workers, execution, UI, live/batch input, cancellation/timeouts, and offline exports | Python 3, `wasm-as`, Chromium/chromedriver, Firefox/geckodriver |
| `git diff --check` | Whitespace before review | Git |

Add descriptively named positive/negative and boundary records to `test/cases.json`.
`test/cases.js` exposes the same JSON to Node; Python reads it directly without Node.
`test/tutorials.json` pins all 23 original examples from `mbono/pascal_tutorial` at
revision `a287978828a2d3a9af18aac2203c9aeb2b3d9d13`. The Node/Python tutorial checks
verify Git blob hashes against shared source records before the ordinary host/WAT/browser
execution checks. Preserve original bytes; put additional variations in separate cases.
Preserve existing input strings, expected program output, and located diagnostics rather
than rewriting batch cases into live scripts. Live scripts belong in
`test/interactive.json`; `test/interactive.py` covers workers, waits, EOF/limits,
controls, and exports. `test/variant-runtime.js` shares forged-schema/lease and atomic
boundary checks between Node and actual browsers. `test/reference-runtime.js` similarly checks address identities,
frame/heap lifetime, dynamic claims and limits; test-only WAT exports also check frame
epoch exhaustion in Node and both browsers. `test/file-runtime.js` and `test/file.js`
cover virtual-file ownership, modes, cursor isolation, cleanup and atomic input.
`test/keyboard.json` / `test/keyboard.py` cover raw-key worker requests, trusted controls,
Unicode chunks, paste/IME, mixed line prefixes, EOF/limits and offline reruns.
`test/keyboard-runtime.js` shares cursor/accounting and forged-scalar boundary checks
between Node and both browsers; `test/keyboard.js` checks on-demand CRT emission. FPC
executables run in the temporary test directory so comparison file I/O stays there.
`test/terminal.py` covers native editing, caret geometry,
selection, draft/focus state, and offline presentation. Private heap boundary checks use
test-only WAT exports; production exports stay unchanged. Profiles are temporary and
retain default browser security; screenshots and verification evidence stay under
`build/`.

FPC comparisons use ObjFPC mode to match unpadded integer output. Compare only where
semantics agree: reference counters, Unicode/dialect extensions, and real formatting
have intentional differences. Prefer compatible converted-integer or Boolean real
results rather than adopting FPC formatting to make a test pass.

## Acceptance gate

For every feature or independently reviewable increment:

1. Resolve dependencies/design gates in SPEC before implementing new public
   behavior. A gate specifies syntax/semantics, representation boundaries, limits,
   diagnostics, and passing/failing examples; planned capability remains unshipped
   until accepted. Preserve the compiler/backend boundary and readable owned code.
2. Add shared passing/rejection cases with exact codes/locations, limits, and
   interactions with aliases, calls, captures, initialization, loops, transfers,
   I/O, and copies. Update the owning references and durable architecture rationale.
3. Build both hosts and run `make test`, `make test-sanitize`, and
   `make test-browser`. Node-only execution is insufficient: compare actual browser
   compiler-worker binary/WAT/diagnostics with native artifacts. Assemble WAT
   independently and compare runtime output/failures; compare FPC where compatible.
4. Exercise relevant example UI and relocated offline HTML in fresh Chromium and
   Firefox profiles. Preserve fresh runs, Stop/timeout/recovery, downloads, safe
   text rendering, cumulative active-time limits, and existing batch hosts. Storage
   changes include budget exhaustion and cleanup/reuse checks.
5. Review function/data-structure documentation, ownership, complexity, dependencies,
   and whitespace. Record commands, results, and unverified limits in the commit/PR
   or release evidence. Mark completion only when the full affected scope passes;
   passing old cases is insufficient evidence for new behavior. Commit the feature
   separately before starting another one.

The following scenario groups preserve the integration acceptance contract:

| Area | Required scenarios |
| --- | --- |
| Compiler/frontend | Independently build/test frontend/IR without Wasm; valid and rejected syntax/type/lvalue cases, scope, source locations, declarations, and complexity boundaries. |
| Artifacts/targets | Default and explicit `wasm32` agree; unsupported targets fail without artifacts; same revision/source/options agree across hosts. |
| WAT | Absent by default; enabling leaves binary bytes unchanged; complete helper/import/data/UTF-8 emission; independent reassembly validates and preserves execution/located failures. |
| Language/storage | Every supported family, value/reference contrasts, partial initialization/copies, evaluation order, recursive frames/captures, counted-control protection, early-return cleanup, arithmetic/index/conversion boundaries, and memory budgets. |
| Heap and addresses | Global/local/result/interior/character addresses, identity limits, stale/reused frame epochs, forwarded ownership, disposal and view identities; recursive structures and aliases; nil/uninitialized/stale/reused pointers; generation/fragmentation/growth limits; borrow/view/reference lifetime and counter-precedence checks; repeat boundary helper checks in both browsers. |
| Virtual files | Typed/text snapshots, exact component identity, names/modes/independent cursors, EOF/EOLN, budgets, receiver evaluation and group borrows, frame/heap retirement, failure/cancellation/completion cleanup, fresh runs, live console interaction, and offline exports. |
| Input/output | Raw CRT keys, key/line cursor mixing, prefix accounting, chunk/EOF boundaries, LF/CRLF, empty/unterminated/trailing lines, invalid/range/EOF/encoding/capacity failures, multi-field reads, shared cursor and legacy imports, Unicode output and literal escaping. |
| Live versus batch | Equivalent supplied lines/EOF yield identical program output and diagnostics without echo; synchronous hosts work without JSPI; actual worker JSPI waits/resumes the same instance; unsupported JSPI offers batch. |
| Sessions/limits | Empty input, EOF closure, stale/duplicate delivery, Stop during compute/wait, compile/run timeouts, long human waits excluded, cumulative active budget not replenished, UTF-8 input/echo/output limits, and fresh recovery/reruns. |
| Terminal | Same-line prompts and visible empty/typed caret; trusted Enter/Escape/native editing; no line IME/paste submission; raw committed chunks once without echo; immutable history, Unicode deletion, focus/selection/draft preservation, scrolling/copying, and empty lines. Supplement functional checks with visual review. |
| UI/artifacts | Version comes from loaded compiler; source edits/failed compilation invalidate WAT/downloads; toggle both WAT states; no stale execution/export; Run/Stop/Reset and repeated recovery work. |
| Static/offline use | Block third-party requests; compile/run/download after asset loading without network; relocate exported HTML, close playground, and open via `file://` in fresh offline profiles without caches/sibling assets. |
| Export safety/notices | Exact program and matching helpers; no execution before Run; Stop/timeout/reruns/input failures and both modes; literal `</script>`/Unicode never executes HTML; complete MIT notice and preserved third-party notices in their distributions. |
| Build/delivery | Build both hosts offline after installing documented SDK/toolchains; comparison tools are not production prerequisites; inspect release assets/requests for external dependencies and unintended capabilities. |

Documentation-only edits require relative-link/anchor, formatting, and relevant
asset-copy checks, not compiler acceptance runs. Preserve unique requirements when
consolidating references; Git retains chronological verification history.

## Coding and review conventions

- Use C11, four-space C indentation, snake_case C/Python, and camelCase JavaScript.
  Optional `clang-format` uses `.clang-format` (100 columns, Linux braces).
- Keep modules/functions focused, names descriptive, and control flow explicit.
  Minimize duplication/complexity while preserving frontend/backend boundaries;
  add abstractions only for current needs, not speculative frameworks/optimizations.
- Comment every function, including internal helpers: purpose and nonobvious inputs,
  results, failures, side effects, and preconditions. Document ownership/lifetimes.
- Comment every struct/union/enum and JavaScript object/message schema with its role,
  invariants, ownership, and units/meaning of nonobvious fields and relationships.
- Put interface contracts beside declarations and implementation detail beside code.
  Explain intent/subtleties rather than narrating operations; update comments with
  structural/behavior changes and review readability throughout implementation.
- Update language/grammar, ABI, architecture, dependency, or contributor references
  when their contracts change. Preserve limits, optional WAT, cancellation, safe
  text, offline exports, and synchronous batch compatibility.

Use concise imperative commit subjects, such as `Implement typed functions`, and commit
completed features separately. PRs explain behavior, milestones, validation, linked
issues where available, and screenshots for UI changes.

## Static hosting

Run `make site` and publish only the following `build/` assets together: `index.html`,
`style.css`, `app.js`, `compiler.js`, `compiler-worker.js`, `export.js`, `pas2wasm.js`,
`pas2wasm.wasm`, `supported-language.md`, `runtime/`, `LICENSE`, and
`THIRD_PARTY_NOTICES.txt`. Do not publish native executables, test binaries, or
screenshots. The language reference is copied from docs while preserving its public URL.
Preserve relative paths and serve Wasm as `application/wasm`.

The GitHub Pages workflow builds and deploys this allowlisted site when a `v*` version
tag is pushed. It can also be run manually from the Actions tab; pushes and pull
requests to `main` do not publish. In repository settings, choose **Settings → Pages
→ Build and deployment → Source → GitHub Actions**. The deploy job publishes to the
`github-pages` environment using the workflow token; no personal access token is needed.
The site is the example playground, while the compiler remains the project.

Static hosting such as GitHub Pages needs no compiler service. The standalone export is
different: it embeds an already compiled program, runtime, worker, console/styles, and
license for offline local-file use. Preserve the [export
requirements](spec.md#standalone-html); do not require sibling requests,
SharedArrayBuffer, isolation headers, or extra capabilities/dependencies.

## Versioning and releases

Pas2Wasm follows [Semantic Versioning 2.0.0](https://semver.org/). [VERSION](../VERSION)
is the single version source; milestone completion does not automatically change it or
create a release/tag. The generated build header embeds that value into native, Wasm,
and sanitizer builds, which rebuild when it changes. The generated header is ignored.
Compiler version and generated-program ABI namespace are versioned separately; feature
releases do not automatically rename `pas2wasm_v1` or change artifact filenames.

| Stage | Policy |
| --- | --- |
| Before 1.0 | Increment minor for feature releases or breaking changes, resetting patch to zero; patch for compatible bug fixes. |
| 1.0 and later | Release 1.0 when the public contract is stable; major for incompatible changes, minor for compatible additions, patch for compatible fixes. |
| Compatibility | Documented Pascal syntax/semantics, CLI options, C/JavaScript APIs, diagnostic codes, and runtime imports. |
| Exclusions | Diagnostic wording, WAT formatting, and identical bytes across different compiler releases are not guarantees. |

Document breaking changes explicitly, including newly reserved names with migration
examples. The [ABI reference](runtime-abi.md#compiler-integration) describes version
accessors and their ownership. The example heading/tab reads the loaded compiler's
version; no runtime version-file fetch or duplicated UI literal is needed. This versions
the compiler, not the Pascal programs it generates.

To prepare a release, update VERSION and [changelog.md](changelog.md), build both hosts,
and run the full acceptance gate above. Commit release changes, then create an annotated
`v<version>` tag. Published versions/tags identify immutable contents; corrections
require another version. SemVer prerelease identifiers such as `<version>-rc.1` are
supported; changing versioning setup alone creates no tag.

## Licensing

Repository-owned code/docs use [MIT](../LICENSE). Retain short copyright and
`SPDX-License-Identifier: MIT` comments in maintained C/header, JavaScript, Python,
HTML, CSS, and Makefile sources. Preserve shebang/encoding positions and appropriate
comment syntax; generated files, data fixtures, and Pascal programs need no headers.

Keep the full project license with site assets and inside exported HTML. Users choose
licenses for their own Pascal programs; embedded Pas2Wasm runtime code retains its MIT
notice. Preserve third-party support licenses/notices, as described in the dependency
inventory; do not replace or edit their legal text.
