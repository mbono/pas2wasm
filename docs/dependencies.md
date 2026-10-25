# Dependencies and licenses

Repository-owned compiler, frontend/IR, Wasm/WAT emitter, runtime, and example
integration use the [MIT license](../LICENSE). There are no third-party parser or
compiler libraries, frontend packages, JavaScript package dependencies, CDNs, or runtime
services. JSPI is a browser API, not an external dependency; synchronous batch mode
requires no JSPI. Partial CRT uses directly emitted Wasm helpers plus standard keyboard,
composition and paste events; no terminal library or precompiled CRT is shipped.
Backspace display uses repository-owned scalar/chunk processing and native DOM text
nodes/caret, without ANSI emulation or another terminal dependency.
Host text-file selection/download uses native File/Blob APIs, UTF-8 TextDecoder,
structured-clone messages and temporary Blob URLs; no filesystem package or service.
The headless engine uses the same runtime/controller, Fetch, AbortController and Blob
workers. Make concatenates its classic script from maintained sources; no JavaScript
bundler, UI package, module loader or runtime dependency is added.
Fixed-decimal formatting uses built-in JavaScript BigInt and DataView,
without a numeric library or locale dependency. Runtime programs gain no host/OS filesystem/network/DOM or
arbitrary-JavaScript access.

## Tool inventory

The table records verified tool versions, purposes, and licenses. Native compiler, Make,
and Emscripten are build tools. Emscripten also supplies shipped loader/C support. Node,
Binaryen, FPC, browsers/drivers, and sanitizers verify development builds; Python also
serves the local example. Formatting is optional. Comparison/ verification tools are not
required to produce release compiler builds.

| Tool/support | Purpose | Version verified | License |
| --- | --- | --- | --- |
| GCC / native system C library | Native build | GCC 16.2.1 | GPL-3.0 with runtime exception / system libc license |
| GNU Make | Build orchestration | 4.4.1 | GPL-3.0 |
| Emscripten / Clang | C-to-Wasm build and generated loader | 6.0.12 | MIT or NCSA; LLVM components have their own notices |
| musl / compiler-rt | Standard C support linked by Emscripten | SDK-bundled | MIT / LLVM license, see notices |
| Node.js | Cross-host test harness only | 26.8.2 | MIT and bundled notices |
| Binaryen wasm-as | Independent WAT and private-heap browser tests only | 133 | Apache-2.0 |
| Free Pascal | Portable program comparison tests only | 3.2.2 | GPL and runtime library exceptions |
| Python | Browser test harness / local development server | 3.14 | PSF |
| Chromium + chromedriver | Browser acceptance tests | 154.0.8037.97 | BSD and bundled notices |
| Firefox + geckodriver | Browser acceptance tests | 158.0 / 0.37.1 | MPL-2.0 and bundled notices |
| clang-format | Optional C formatting | Installed developer tool | Apache-2.0 with LLVM exception |
| browser-actions/setup-chrome | CI-only stable Chrome and matching driver setup | Action v2; installed versions recorded by CI | MIT |

## Delivery and dependency policy

- Prefer repository-owned code and standard C/browser facilities. Core parsing,
  numeric/UTF-8 helpers, and heap ownership are maintained here; no external
  allocator, libm/parser package, or Unicode database is required by generated
  programs. Explicit address identities use a bounded per-execution registry built
  from standard JavaScript Map/BigInt; it stores metadata rather than Pascal heap objects.
  Virtual files use bounded Map/Uint8Array storage and shared UTF-8/parser helpers.
  No filesystem package, Emscripten filesystem, WASI or persistent storage is added.
- New dependencies require a documented necessity, version, license, and explicit
  build/test/runtime classification. No parser generator, frontend framework,
  bundler, WAT assembler, or package manager is a production build requirement.
- Once documented toolchains and SDK components are installed, native/Wasm builds
  work offline. Make performs no SDK bootstrap or dependency installation.
- Ship all assets together. No runtime CDN scripts, fonts, analytics, services,
  accounts, or dependency downloads. Compiler assets may load from their deployment;
  standalone exports must require no sibling requests or cached deployment.
- Toolchain-generated compiler JS/Wasm includes Emscripten, musl, and compiler-rt
  support. Include the full [third-party notices](third-party-notices.txt) unchanged
  in the built site. Exceptions adding shipped third-party code must likewise
  bundle its licenses/notices, never require a separate runtime installation.
- Include root LICENSE in site assets. Standalone HTML embeds the full MIT notice
  and only the generated program plus repository-owned runtime; it ships no
  Emscripten compiler/loader. Preserve other third-party components' own licenses.
- CI setup actions download development toolchains/browsers on GitHub runners only;
  this does not change offline production builds or ship those actions to consumers.
  [setup-chrome](https://github.com/browser-actions/setup-chrome) is used only by the
  browser validation job; Emscripten setup retains the existing pinned SDK.

Detailed browser/export contracts are in [SPEC](spec.md); architecture and exact runtime
ownership/layouts are documented separately rather than repeated here.
