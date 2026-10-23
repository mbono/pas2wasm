# Repository Guidelines

## Required reading

Read [docs/spec.md](docs/spec.md) before feature work. Follow its product invariants
and resolve future design gates before coding. The [language reference and formal
grammar](docs/supported-language.md) and [ABI reference](docs/runtime-abi.md) define
current behavior; preserve them unless a change is explicitly specified.

Use [contributing.md](docs/contributing.md) for layout, build/test commands,
conventions, acceptance, hosting, and releases. Record durable design rationale
in [architecture.md](docs/architecture.md); use Git/PR history for verification logs.
Do not hand-edit generated files in `build/`.

## Implementation constraints

- Maintain native and Wasm compiler hosts. Only Wasm output is implemented;
  future CPU/OS backends belong behind the backend interface.
- Keep frontend/IR free of Wasm layout, instructions, and host details.
- Keep code small and readable: C11, four-space C indentation, snake_case C/Python,
  camelCase JavaScript, and `.clang-format`. Comment functions/data structures,
  ownership, limits, and subtle invariants.
- Preserve fresh worker memories, cancellation/recovery, limits, text-only
  rendering, opt-in WAT, and offline `file://` exports.
- Live input uses browser JSPI; synchronous batch hosts require none. Human input
  waits do not consume the cumulative execution budget. Pascal gains no filesystem,
  network, DOM, or arbitrary-JavaScript capability.
- Read the version from the loaded compiler. VERSION is embedded at build time;
  bump per release, update [changelog.md](docs/changelog.md), and follow the
  [release policy](docs/contributing.md#versioning-and-releases).

## Completion and review

Update the owning language/grammar, ABI, architecture, dependency, and contributor
references when their contracts change. Add descriptive shared/boundary/interaction
cases and preserve existing batch cases. Feature completion requires the full
[acceptance gate](docs/contributing.md#acceptance-gate), including both hosts, WAT
roundtrips, sanitizers, actual Chromium/Firefox workers, and relevant offline exports.
Compare FPC only where semantics agree; documentation-only edits need document
checks rather than compiler tests. Use imperative commit subjects, commit completed
features separately, and run `git diff --check` before review.
