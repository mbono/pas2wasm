# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

# Both compiler hosts use the same frontend and backend. GNU Make >= 4.3.
CC ?= cc
EMCC ?= emcc
NODE ?= node
PYTHON ?= python3
CPPFLAGS ?= -Isrc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
LDFLAGS ?=
EMFLAGS ?= -sMODULARIZE=1 -sEXPORT_NAME=Pas2Wasm -sENVIRONMENT=web,worker,node \
           -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=2097152 \
           -sFILESYSTEM=0 -sINCOMING_MODULE_JS_API='["wasmBinary"]' --no-entry
EXPORTS = '["_malloc","_free","_p2w_version","_p2w_compile","_p2w_free_result","_p2w_binary","_p2w_binary_size","_p2w_wat","_p2w_code","_p2w_message","_p2w_line","_p2w_column"]'
FRONTEND = src/ir/program.c src/ir/number.c src/frontend/parser.c \
           src/frontend/constant.c src/frontend/real.c src/frontend/effects.c
CORE = src/buffer.c src/compiler.c $(FRONTEND) \
       src/backend/backend.c src/backend/wasm/layout.c src/backend/wasm/emitter.c \
       src/backend/wasm/heap.c src/backend/wasm/variant.c src/backend/wasm/reference.c
HEADERS = src/pas2wasm.h src/ir/program.h src/ir/number.h src/backend/backend.h \
          src/backend/wasm/layout.h src/backend/wasm/emitter.h src/backend/wasm/variant.h \
          src/backend/wasm/reference.h
WEB = index.html style.css app.js compiler.js compiler-worker.js export.js runtime/host.js runtime/terminal.js runtime/session.js
WEB_OUTPUTS = $(addprefix build/,$(WEB)) build/supported-language.md build/THIRD_PARTY_NOTICES.txt build/LICENSE

.PHONY: all native wasm site test test-sanitize test-browser serve clean help
all: native site
native: build/pas2wasm
wasm: build/pas2wasm.js build/pas2wasm.wasm
site: wasm $(WEB_OUTPUTS)

build:
	mkdir -p $@

# Embed the single version source; no version file is needed at runtime.
build/version.h: VERSION Makefile | build
	@printf '#define P2W_VERSION "%s"\n' "$$(cat VERSION)" > $@

build/pas2wasm: $(CORE) src/driver/main.c $(HEADERS) build/version.h Makefile | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) $(CORE) src/driver/main.c $(LDFLAGS) -o $@

# The loader and binary are one grouped output, rebuilt if either is missing.
build/pas2wasm.js build/pas2wasm.wasm &: $(CORE) $(HEADERS) build/version.h Makefile | build
	$(EMCC) $(CPPFLAGS) -Ibuild $(CFLAGS) $(CORE) $(LDFLAGS) $(EMFLAGS) \
	  -sEXPORTED_FUNCTIONS=$(EXPORTS) -sEXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString","HEAPU8"]' -o build/pas2wasm.js

build/%: web/%
	mkdir -p $(dir $@)
	cp $< $@

build/supported-language.md: docs/supported-language.md | build
	cp $< $@

build/LICENSE: LICENSE | build
	cp $< $@

build/THIRD_PARTY_NOTICES.txt: docs/third-party-notices.txt | build
	cp $< $@

build/frontend-test: test/frontend.c $(FRONTEND) $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) test/frontend.c $(FRONTEND) $(LDFLAGS) -o $@

build/layout-test: test/layout.c src/backend/wasm/layout.c src/ir/program.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) test/layout.c src/backend/wasm/layout.c src/ir/program.c $(LDFLAGS) -o $@

build/layout-test-sanitize: test/layout.c src/backend/wasm/layout.c src/ir/program.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	  test/layout.c src/backend/wasm/layout.c src/ir/program.c $(LDFLAGS) -o $@

build/number-test: test/number.c src/frontend/real.c src/ir/number.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) test/number.c src/frontend/real.c src/ir/number.c $(LDFLAGS) -o $@

build/number-test-sanitize: test/number.c src/frontend/real.c src/ir/number.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	  test/number.c src/frontend/real.c src/ir/number.c $(LDFLAGS) -o $@

test: all build/frontend-test build/layout-test build/number-test
	./build/frontend-test
	./build/layout-test
	./build/number-test
	$(NODE) test/acceptance.js

build/pas2wasm-sanitize: $(CORE) src/driver/main.c $(HEADERS) build/version.h Makefile | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer \
	  $(CORE) src/driver/main.c $(LDFLAGS) -o $@

test-sanitize: all build/pas2wasm-sanitize build/layout-test-sanitize build/number-test-sanitize
	./build/layout-test-sanitize
	./build/number-test-sanitize
	P2W_NATIVE=./build/pas2wasm-sanitize $(NODE) test/acceptance.js

test-browser: all
	$(PYTHON) test/browser.py

serve: site
	$(PYTHON) -m http.server 8000 --directory build

clean:
	rm -rf build

help:
	@printf '%s\n' 'make: native compiler and static browser application' \
	 'make test: frontend, native/Wasm parity, runtime, WAT, and FPC checks' \
	 'make test-sanitize: repeat acceptance with C memory/undefined-behavior checks' \
	 'make test-browser: actual browser-worker and UI acceptance checks' \
	 'make serve: serve the playground at http://localhost:8000' \
	 'make clean: remove generated files'
