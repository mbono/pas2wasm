/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Create a version-1 runtime with private input/output state. Imports expose only
 * I/O and located failure reporting; attach supplies the instance's own memory.
 * This function is self-contained so it can be embedded in exported HTML. */
function createPas2WasmRuntime(input, emit, limits = {}, requestLine = null) {
    const inputLimit = limits.input ?? 1048576;
    const outputLimit = limits.output ?? 1048576;
    const encoder = new TextEncoder();
    const decoder = new TextDecoder('utf-8', {fatal: true, ignoreBOM: true});
    let memory = null, position = 0, offset = 0, outputBytes = 0;
    let closed = !requestLine, inputBytes = encoder.encode(input).length;
    const lines = input === '' ? [] : input.split(/\r?\n/);
    if (input.endsWith('\n')) lines.pop();
    const terminated = lines.map((_, i) => i < lines.length - 1 || input.endsWith('\n'));
    /* Throw a diagnostic-bearing error; callers must render its fields as text. */
    function fail(code, message, line = 0, column = 0) {
        const error = new Error(message);
        error.code = code; error.line = line; error.column = column;
        throw error;
    }
    if (encoder.encode(input).length > inputLimit)
        fail('R_INPUT_LIMIT', 'Program input exceeds the input limit.');
    /* Bound output by UTF-8 bytes before forwarding any new text. */
    function write(text) {
        outputBytes += encoder.encode(text).length;
        if (outputBytes > outputLimit) fail('R_OUTPUT_LIMIT', 'Program output exceeds the output limit.');
        emit(text);
    }
    /* Choose an explicit decimal precision/notation with stable rounding and -0. */
    function formatReal(value) {
        if (value === 0) return Object.is(value, -0) ? '-0' : '0';
        const magnitude = Math.abs(value);
        let scientific;
        for (let precision = 1; precision <= 17; precision++) {
            scientific = magnitude.toExponential(precision - 1);
            if (Number(scientific) === magnitude) break;
        }
        const parts = scientific.split('e');
        let digits = parts[0].replace('.', '').replace(/0+$/, '');
        const exponent = Number(parts[1]);
        let text;
        if (exponent >= -6 && exponent < 21) {
            const point = exponent + 1;
            if (point <= 0) text = '0.' + '0'.repeat(-point) + digits;
            else if (point >= digits.length) text = digits + '0'.repeat(point - digits.length);
            else text = digits.slice(0, point) + '.' + digits.slice(point);
        } else {
            text = digits[0] + (digits.length > 1 ? '.' + digits.slice(1) : '') +
                'e' + (exponent >= 0 ? '+' : '') + String(exponent);
        }
        return value < 0 ? '-' + text : text;
    }
    /* Fetch one line only when needed. The optional provider owns delivery, not
     * parsing; null closes it permanently. Batch imports never return Promises. */
    function ensureLine(line, column) {
        if (position < lines.length) return;
        if (closed) fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
        return Promise.resolve(requestLine(line, column)).then(text => {
            if (text === null) {
                closed = true;
                fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
            }
            if (typeof text !== 'string' || /[\r\n]/.test(text))
                fail('R_INPUT_INVALID', 'Expected one submitted input line.', line, column);
            inputBytes += encoder.encode(text).length + 1;
            if (inputBytes > inputLimit) fail('R_INPUT_LIMIT', 'Program input exceeds the input limit.', line, column);
            lines.push(text);
            terminated.push(true);
        });
    }
    /* Map a possibly suspended operation without making batch callers async. */
    function mapped(value, action) {
        return value instanceof Promise ? value.then(action) : action(value);
    }
    /* Consume the remaining logical line, including an empty remainder. */
    function nextLine(line, column) {
        return mapped(ensureLine(line, column), () => {
            const text = lines[position].slice(offset);
            position++; offset = 0;
            return text;
        });
    }
    /* Existing numeric syntax/range rules shared by line and stream reads. */
    function parseNumber(text, real, line, column) {
        const pattern = real ? /^[ \t]*([+-]?[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)(?=[ \t]|$)/
                             : /^[ \t]*([+-]?[0-9]+)(?=[ \t]|$)/;
        const match = pattern.exec(text);
        if (!match) fail('R_INPUT_INVALID', real ? 'Expected a real at the start of the input line.' :
            'Expected an integer at the start of the input line.', line, column);
        const value = Number(match[1]);
        if (real ? !Number.isFinite(value) || (value === 0 && /[1-9]/.test(match[1].split(/[eE]/)[0])) :
            !Number.isInteger(value) || value < -2147483648 || value > 2147483647)
            fail('R_INPUT_RANGE', real ? 'Input real is outside the finite nonzero range.' :
                'Input integer is outside the signed 32-bit range.', line, column);
        return {value, length:match[0].length};
    }
    /* Numeric read may skip lines; readln fields must stay on their current line.
     * A loop bounds stack use even for a batch containing many blank lines. */
    function streamNumber(mode, real, line, column) {
        for (;;) {
            const ready = ensureLine(line, column);
            if (ready instanceof Promise) return ready.then(() => streamNumber(mode, real, line, column));
            const remaining = lines[position].slice(offset);
            if (mode && offset > 0 && !terminated[position] && /^[ \t]*$/.test(remaining))
                fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
            if (!mode && /^[ \t]*$/.test(remaining)) {
                position++; offset = 0;
                continue;
            }
            const result = parseNumber(remaining, real, line, column);
            offset += result.length;
            return result.value;
        }
    }
    /* Preserve Unicode scalar semantics, including whitespace and normalized LF. */
    function streamCharacter(mode, line, column) {
        return mapped(ensureLine(line, column), () => {
            const value = lines[position].codePointAt(offset);
            if (value === undefined) {
                if (!terminated[position]) fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
                if (mode) fail('R_INPUT_INVALID', 'Expected a character.', line, column);
                position++; offset = 0;
                return 10;
            }
            if (value >= 0xd800 && value <= 0xdfff)
                fail('R_ENCODING', 'Input contains an invalid Unicode surrogate.', line, column);
            offset += value > 0xffff ? 2 : 1;
            return value;
        });
    }
    /* Validate input before encoding so lone surrogates are never replaced. */
    function inputString(text, destination, capacity, line, column) {
        for (const scalar of text) {
            const value = scalar.codePointAt(0);
            if (value >= 0xd800 && value <= 0xdfff)
                fail('R_ENCODING', 'Input contains an invalid Unicode surrogate.', line, column);
        }
        storeString(destination, capacity, encoder.encode(text), line, column);
    }
    /* Borrow only a checked range in this program's own bounded linear memory. */
    function bytes(pointer, length, line, column) {
        if (!memory || pointer < 0 || length < 0 || pointer > memory.buffer.byteLength - length)
            fail('R_MEMORY', 'Invalid string memory range.', line, column);
        return new Uint8Array(memory.buffer, pointer, length);
    }
    /* Validate initialized headers before borrowing immutable UTF-8 value bytes. */
    function stringBytes(pointer, line, column) {
        const header = bytes(pointer, 16, line, column);
        if (!header[8]) fail('R_UNINITIALIZED', 'String was read before initialization.', line, column);
        const view = new DataView(memory.buffer, pointer, 16);
        const length = view.getUint32(0, true), capacity = view.getUint32(4, true);
        if (capacity > 1048576 || length > capacity)
            fail('R_MEMORY', 'Invalid string header.', line, column);
        bytes(pointer + 16, capacity, line, column);
        return bytes(pointer + 16, length, line, column);
    }
    /* Check a complete write before changing bytes, including overlapping aliases. */
    function storeString(pointer, capacity, value, line, column) {
        if (capacity < 0 || capacity > 1048576)
            fail('R_MEMORY', 'Invalid string capacity.', line, column);
        if (value.length > capacity)
            fail('R_STRING_CAPACITY', 'String exceeds its UTF-8 byte capacity.', line, column);
        bytes(pointer, 16 + capacity, line, column);
        bytes(pointer + 16, value.length, line, column).set(value);
        const view = new DataView(memory.buffer, pointer, 16);
        view.setUint32(0, value.length, true);
        view.setUint32(4, capacity, true);
        view.setUint8(8, 1);
    }
    /* Decode strictly; no Unicode normalization or replacement characters are inserted. */
    function stringText(pointer, line, column) {
        const value = stringBytes(pointer, line, column);
        try { return decoder.decode(value); }
        catch { fail('R_ENCODING', 'String is not valid UTF-8.', line, column); }
    }
    /* Every source-level char is a Unicode scalar, with no surrogate code points. */
    function character(value, line, column) {
        if (!Number.isInteger(value) || value < 0 || value > 0x10ffff ||
            (value >= 0xd800 && value <= 0xdfff))
            fail('R_CHARACTER', 'Invalid Unicode scalar value.', line, column);
        return String.fromCodePoint(value);
    }
    /* A view borrows a caller-owned descriptor; no host reference survives a call. */
    function characterView(target, line, column) {
        const pointer = target & 0x7fffffff;
        bytes(pointer, 16, line, column);
        const view = new DataView(memory.buffer, pointer, 16);
        return {source:view.getUint32(0, true), index:view.getUint32(4, true)};
    }
    /* Resolve scalar positions afresh after alias changes to UTF-8 byte widths. */
    function stringCharacters(source, index, line, column) {
        const characters = Array.from(stringText(source, line, column));
        if (index < 1 || index > characters.length)
            fail('R_BOUNDS', 'Character index is outside the string.', line, column);
        return characters;
    }
    /* Strict scalar slices never clamp negative, past-end, or excessive counts. */
    function stringSlice(source, start, count, line, column) {
        const characters = Array.from(stringText(source, line, column));
        if (start < 1 || start > characters.length + 1 || count < 0 ||
            count > characters.length - start + 1)
            fail('R_BOUNDS', 'String slice is outside its bounds.', line, column);
        return encoder.encode(characters.slice(start - 1, start - 1 + count).join(''));
    }
    /* ABI record: name/signature pairs are documented in docs/runtime-abi.md. */
    const imports = {
        /* Read only validated ranges in the generated program's linear memory. */
        write_utf8(pointer, length) {
            if (!memory || pointer < 0 || length < 0 || pointer > memory.buffer.byteLength - length)
                fail('R_MEMORY', 'Program requested an invalid memory range.');
            try { write(decoder.decode(new Uint8Array(memory.buffer, pointer, length))); }
            catch (error) {
                if (error.code) throw error;
                fail('R_ENCODING', 'Program output is not valid UTF-8.');
            }
        },
        /* Preserve Pascal's decimal representation. */
        write_i32(value) { write(String(value)); },
        /* Finite binary64 output uses the frozen repository decimal formatter. */
        write_f64(value) {
            if (!Number.isFinite(value)) fail('R_REAL_RANGE', 'Cannot print a nonfinite real.');
            write(formatReal(value));
        },
        /* Boolean output uses Pascal's uppercase spelling. */
        write_bool(value) { write(value ? 'TRUE' : 'FALSE'); },
        /* Each writeln appends precisely one LF. */
        newline() { write('\n'); },
        /* Legacy line-consuming imports retain their signatures and behavior. */
        read_i32(line, column) {
            return mapped(nextLine(line, column), text => parseNumber(text, false, line, column).value);
        },
        read_f64(line, column) {
            return mapped(nextLine(line, column), text => parseNumber(text, true, line, column).value);
        },
        discard_line(line, column) { return mapped(nextLine(line, column), () => {}); },
        /* Stream imports leave the trailing delimiter available to later reads. */
        stream_i32(mode, line, column) { return streamNumber(mode, false, line, column); },
        stream_f64(mode, line, column) { return streamNumber(mode, true, line, column); },
        stream_char(mode, line, column) { return streamCharacter(mode, line, column); },
        stream_string(destination, capacity, mode, line, column) {
            return mapped(ensureLine(line, column), () => {
                if (offset > 0 && offset === lines[position].length && !terminated[position])
                    fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
                const text = lines[position].slice(offset);
                inputString(text, destination, capacity, line, column);
                offset = lines[position].length;
            });
        },
        string_assign(destination, capacity, source, line, column) {
            storeString(destination, capacity, stringBytes(source, line, column), line, column);
        },
        /* Both operands are already snapshotted by the compiler before concatenation. */
        string_concat(destination, capacity, left, right, line, column) {
            const a = stringBytes(left, line, column), b = stringBytes(right, line, column);
            if (a.length + b.length > capacity)
                fail('R_STRING_CAPACITY', 'Concatenation exceeds its UTF-8 byte capacity.', line, column);
            const value = new Uint8Array(a.length + b.length);
            value.set(a); value.set(b, a.length);
            storeString(destination, capacity, value, line, column);
        },
        /* Valid UTF-8 unsigned byte order equals Unicode scalar lexicographic order. */
        string_compare(left, right, line, column) {
            const a = stringBytes(left, line, column), b = stringBytes(right, line, column);
            for (let i = 0; i < Math.min(a.length, b.length); i++)
                if (a[i] !== b[i]) return a[i] < b[i] ? -1 : 1;
            return Math.sign(a.length - b.length);
        },
        /* Count scalars, including combining marks individually rather than graphemes. */
        string_length(source, line, column) {
            let count = 0;
            for (const character of stringText(source, line, column)) count++;
            return count;
        },
        /* Whole-line reads preserve whitespace and preflight encoding/capacity atomically. */
        string_read(destination, capacity, line, column) {
            return mapped(nextLine(line, column), text => inputString(text, destination, capacity, line, column));
        },
        /* String output uses the existing text-only, byte-limited writer. */
        string_write(source, line, column) { write(stringText(source, line, column)); },
        /* Read-only indexed values use the same scalar boundary checks as views. */
        string_index(source, index, line, column) {
            return stringCharacters(source, index, line, column)[index - 1].codePointAt(0);
        },
        /* Normal char cells and tagged views share one reference ABI. */
        char_load(target, line, column) {
            if (target < 0) {
                const view = characterView(target, line, column);
                return imports.string_index(view.source, view.index, line, column);
            }
            const data = bytes(target, 16, line, column);
            if (!data[8]) fail('R_UNINITIALIZED', 'Character was read before initialization.', line, column);
            const value = new DataView(memory.buffer, target, 16).getInt32(0, true);
            character(value, line, column);
            return value;
        },
        /* Variable-width replacement preflights the complete new string before writing. */
        char_store(target, value, line, column) {
            const text = character(value, line, column);
            if (target < 0) {
                const view = characterView(target, line, column);
                const characters = stringCharacters(view.source, view.index, line, column);
                characters[view.index - 1] = text;
                const capacity = new DataView(memory.buffer, view.source, 16).getUint32(4, true);
                storeString(view.source, capacity, encoder.encode(characters.join('')), line, column);
            } else {
                bytes(target, 16, line, column);
                const data = new DataView(memory.buffer, target, 16);
                data.setInt32(0, value, true); data.setUint8(8, 1);
            }
        },
        /* Character output preserves exactly one scalar, including NUL and BOM. */
        write_char(value, line, column) { write(character(value, line, column)); },
        /* Read one scalar without skipping whitespace, then discard the rest of its line. */
        read_char(line, column) {
            return mapped(nextLine(line, column), text => {
                const value = text.codePointAt(0);
                if (value === undefined) fail('R_INPUT_INVALID', 'Expected a character.', line, column);
                if (value >= 0xd800 && value <= 0xdfff)
                    fail('R_ENCODING', 'Input contains an invalid Unicode surrogate.', line, column);
                return value;
            });
        },
        /* Char-to-string widening produces an independently owned program-memory value. */
        string_char(destination, capacity, value, line, column) {
            storeString(destination, capacity, encoder.encode(character(value, line, column)), line, column);
        },
        /* Query the exact bounded result size before the compiler reserves its lease. */
        string_slice_size(source, start, count, line, column) {
            return stringSlice(source, start, count, line, column).length;
        },
        /* Copy a validated scalar slice into caller-owned storage. */
        string_slice(destination, capacity, source, start, count, line, column) {
            storeString(destination, capacity, stringSlice(source, start, count, line, column), line, column);
        },
        /* Map generated numeric failure codes to stable user-facing diagnostics. */
        fail(code, line, column) {
            const errors = {
                1: ['R_UNINITIALIZED', 'Variable was read before initialization.'],
                2: ['R_OVERFLOW', 'Signed 32-bit integer overflow.'],
                3: ['R_DIV_ZERO', 'Division by zero.'],
                4: ['R_DIV_DOMAIN', 'div/mod require a nonnegative dividend and a positive divisor.'],
                5: ['R_CALL_DEPTH', 'Routine call depth exceeds 64.'],
                6: ['R_RESULT', 'Function completed without assigning a result.'],
                7: ['R_MEMORY', 'Program storage exceeds its memory budget.'],
                8: ['R_BOUNDS', 'Index or slice is outside its bounds.'],
                9: ['R_FOR_CONTROL', 'An alias would modify an active counted control.'],
                10: ['R_REAL_RANGE', 'Real arithmetic produced a nonfinite result.'],
                11: ['R_CONVERSION', 'Real conversion exceeds the signed 32-bit integer range.'],
                12: ['R_STRING_CAPACITY', 'String exceeds its UTF-8 byte capacity.'],
                13: ['R_CHARACTER', 'Invalid Unicode scalar value.'],
                14: ['R_NIL', 'Cannot dereference nil.'],
                15: ['R_DANGLING', 'Pointer refers to a released allocation.'],
                16: ['R_DISPOSE', 'Cannot dispose an invalid, nil, or released pointer.'],
                17: ['R_LIFETIME', 'Cannot dispose an allocation while it is borrowed.'],
                18: ['R_POINTER', 'Invalid internal pointer or allocation bounds.']
            };
            const entry = errors[code] || ['R_FAILURE', 'Unknown program runtime failure.'];
            fail(entry[0], entry[1], line, column);
        }
    };
    return {imports,
        /* Bind only the freshly instantiated generated program's bounded memory. */
        attach(instance) {
            memory = instance.exports.memory;
            if (!(memory instanceof WebAssembly.Memory) || memory.buffer.byteLength > 67108864)
                fail('R_MEMORY', 'Program memory exceeds 64 MiB or is missing.');
        }
    };
}

/* One fresh program per worker. Matching run/request identities are required to
 * deliver a line; JSPI suspension yields the worker event loop without replay. */
function pas2WasmProgramWorker() {
    let active = false, runId, waiting = null, sequence = 0;
    const supported = () => typeof WebAssembly.Suspending === 'function' &&
        typeof WebAssembly.promising === 'function';
    onmessage = async ({data}) => {
        if (data.type === 'probe') {
            postMessage({type:'capabilities', id:data.id, jspi:supported()});
            return;
        }
        if (data.type === 'input' || data.type === 'end-input') {
            if (!waiting || data.id !== runId || data.requestId !== waiting.id) return;
            const resolve = waiting.resolve;
            waiting = null;
            postMessage({type:'running', id:runId});
            resolve(data.type === 'end-input' ? null : data.text);
            return;
        }
        if (active) return;
        active = true; runId = data.id;
        let pending = '', firstOutput = true, flushedAt = performance.now();
        /* Batch bounded output, but always reveal a prompt before suspension. */
        function flush() {
            if (pending) {
                postMessage({type:'output', id:runId, text:pending}); pending = '';
                flushedAt = performance.now();
            }
        }
        /* A provider requests one line. The runtime handles limits and EOF. */
        function requestLine(line, column) {
            flush();
            return new Promise(resolve => {
                waiting = {id:++sequence, resolve};
                postMessage({type:'input-request', id:runId, requestId:waiting.id, line, column});
            });
        }
        try {
            const jspi = supported();
            postMessage({type:'capabilities', id:runId, jspi});
            if (data.interactive && !jspi)
                throw Object.assign(new Error('Interactive console requires browser JSPI support. Choose Batch input.'), {code:'E_JSPI'});
            const runtime = createPas2WasmRuntime(data.input || '', text => {
                pending += text;
                if (firstOutput || pending.length >= 4096 || performance.now() - flushedAt >= 16) {
                    flush(); firstOutput = false;
                }
            }, data.limits, data.interactive ? requestLine : null);
            const imports = {...runtime.imports};
            if (data.interactive) {
                for (const name of ['read_i32','read_f64','read_char','string_read','discard_line',
                    'stream_i32','stream_f64','stream_char','stream_string'])
                    imports[name] = new WebAssembly.Suspending(imports[name]);
            }
            const {instance} = await WebAssembly.instantiate(data.binary, {pas2wasm_v1:imports});
            runtime.attach(instance);
            postMessage({type:'running', id:runId});
            if (data.interactive) await WebAssembly.promising(instance.exports.run)();
            else instance.exports.run();
            flush(); postMessage({type:'done', id:runId});
        } catch (error) {
            flush(); postMessage({type:'error', id:runId, diagnostic:{
                code:error.code || 'R_TRAP', message:error.message,
                line:error.line || 0, column:error.column || 0
            }});
        } finally { waiting = null; }
    };
}

/* Build the embedded worker without imports, network requests, or shared memory. */
function pas2WasmWorkerSource() {
    return `const createPas2WasmRuntime = ${createPas2WasmRuntime.toString()};\n(${pas2WasmProgramWorker.toString()})();`;
}
if (typeof module !== 'undefined' && module.exports)
    module.exports = {createPas2WasmRuntime, pas2WasmWorkerSource};
