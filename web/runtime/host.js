/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

/* Create a version-1 runtime with private input/output and pseudorandom state.
 * Imports expose bounded services and failures; attach supplies the instance's own memory.
 * This function is self-contained so it can be embedded in exported HTML. */
function createPas2WasmRuntime(input, emit, limits = {}, requestLine = null) {
    const inputLimit = limits.input ?? 1048576;
    const outputLimit = limits.output ?? 1048576;
    const encoder = new TextEncoder();
    const decoder = new TextDecoder('utf-8', {fatal: true, ignoreBOM: true});
    const variantSchemas = new Map(); // Immutable literal schemas, private to this attached instance.
    let variantSchemaBytes = 0;
    // Identities own only checked memory coordinates/claims; IDs never repeat in a run.
    const references = new Map(), referenceKeys = new Map(), frames = new Map();
    const referenceLimit = limits.references ?? 65536;
    let referenceSequence = 0;
    // Files own bounded snapshots; bindings own names, modes/cursors and checked lifetimes.
    const virtualFiles = new Map(), fileBindings = new Map();
    let fileSequence = 0, fileBytes = 0, openFiles = 0;
    let fileResults = [], fileLoadAllowed = false;

    let memory = null, position = 0, offset = 0, outputBytes = 0;
    let closed = !requestLine, inputBytes = encoder.encode(input).length;
    let keyQueue = '', keyOffset = 0; // Accepted raw chunks share the cumulative input allowance.
    let lines = input === '' ? [] : input.split(/\r?\n/);
    if (input.endsWith('\n')) lines.pop();
    let terminated = lines.map((_, i) => i < lines.length - 1 || input.endsWith('\n'));
    /* Throw a diagnostic-bearing error; callers must render its fields as text. */
    function fail(code, message, line = 0, column = 0) {
        finishFiles(); fileResults = []; fileLoadAllowed = false;
        const error = new Error(message);
        error.code = code; error.line = line; error.column = column;
        throw error;
    }
    if (!Number.isInteger(referenceLimit) || referenceLimit < 1 || referenceLimit > 65536)
        fail('R_MEMORY', 'Address identity limit must be 1..65536.');
    if (encoder.encode(input).length > inputLimit)
        fail('R_INPUT_LIMIT', 'Program input exceeds the input limit.');
    const configuredSeed = limits.randomSeed;
    if (configuredSeed !== undefined &&
        (!Number.isInteger(configuredSeed) || configuredSeed < 0 || configuredSeed > 0xffffffff))
        fail('R_RANDOM_RANGE', 'Random seed must be an unsigned 32-bit integer.');
    let randomState = (configuredSeed ?? 1) || 1;
    /* Xorshift32 visits every nonzero u32. Reject the incomplete final bucket
     * before reducing to the bound, avoiding modulo bias without external services. */
    function randomInteger(bound, line, column) {
        if (!Number.isInteger(bound) || bound < 1 || bound > 2147483647)
            fail('R_RANDOM_RANGE', 'Random bound must be 1..2147483647.', line, column);
        const ceiling = 0xffffffff - (0xffffffff % bound);
        let value;
        do {
            randomState ^= randomState << 13;
            randomState ^= randomState >>> 17;
            randomState ^= randomState << 5;
            randomState >>>= 0;
            value = randomState - 1;
        } while (value >= ceiling);
        return value % bound;
    }
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
    /* Round the exact finite binary64 value to fixed decimals, ties to even.
     * Bounded BigInt arithmetic avoids locale, exponent shortcuts, and toFixed's
     * precision/large-value restrictions. The caller caps precision at 1000. */
    function formatFixed(value, precision) {
        const negative = value < 0 || Object.is(value, -0);
        const view = new DataView(new ArrayBuffer(8));
        view.setFloat64(0, Math.abs(value), true);
        const bits = view.getBigUint64(0, true);
        const exponentBits = Number(bits >> 52n);
        let significand = bits & ((1n << 52n) - 1n);
        const exponent = exponentBits ? exponentBits - 1075 : -1074;
        if (exponentBits) significand |= 1n << 52n;
        let scaled = significand * 10n ** BigInt(precision);
        if (exponent >= 0) scaled <<= BigInt(exponent);
        else {
            const divisor = 1n << BigInt(-exponent);
            const quotient = scaled / divisor, remainder = scaled % divisor;
            scaled = quotient;
            if (remainder * 2n > divisor ||
                (remainder * 2n === divisor && (quotient & 1n))) scaled++;
        }
        let text = scaled.toString();
        if (precision) {
            text = text.padStart(precision + 1, '0');
            text = text.slice(0, -precision) + '.' + text.slice(-precision);
        }
        return (negative ? '-' : '') + text;
    }
    /* Format one complete field within the caller's byte budget. Width counts
     * Unicode scalars; console and virtual text files keep separate allowances. */
    function formattedValue(kind, value, width, precision, line, column,
                            available = outputLimit - outputBytes, limitCode = 'R_OUTPUT_LIMIT') {
        if (!Number.isInteger(width) || width < 0 || width > 1048576 ||
            !Number.isInteger(precision) || precision < 0 || precision > 1000 ||
            (kind !== 5 && precision !== 0))
            fail('R_FORMAT', 'Invalid output width or decimal precision.', line, column);
        let text;
        if (kind === 1 || kind === 5) {
            if (!Number.isFinite(value))
                fail('R_REAL_RANGE', 'Cannot print a nonfinite real.', line, column);
            text = kind === 5 ? formatFixed(value, precision) : formatReal(value);
        } else if (kind === 0 && Number.isInteger(value) &&
                   value >= -2147483648 && value <= 2147483647) text = String(value);
        else if (kind === 2 && (value === 0 || value === 1)) text = value ? 'TRUE' : 'FALSE';
        else if (kind === 3) text = character(value, line, column);
        else if (kind === 4 && Number.isInteger(value)) text = stringText(value, line, column);
        else fail('R_FORMAT', 'Invalid formatted value.', line, column);
        let characterCount = 0;
        for (const scalar of text) characterCount++;
        const padding = Math.max(0, width - characterCount);
        if (padding + encoder.encode(text).length > available)
            fail(limitCode, 'Formatted output exceeds its byte budget.', line, column);
        return ' '.repeat(padding) + text;
    }
    /* Release consumed chunk storage promptly; offsets count UTF-16 units while
     * each delivered value is a complete Unicode scalar. */
    function consumeKeys(units) {
        keyOffset += units;
        if (keyOffset === keyQueue.length) {keyQueue = '';keyOffset = 0;}
    }
    /* Fetch a line only when needed. Complete queued key lines stay synchronous;
     * an unfinished suffix is offered to the line editor as an already paid prefix.
     * File cursors set closed=true, so they never consume this console queue. */
    function ensureLine(line, column) {
        if (position < lines.length) return;
        let prefix = '';
        if (!closed && keyQueue) {
            const end = keyQueue.indexOf('\r',keyOffset);
            if (end >= 0) {
                lines.push(keyQueue.slice(keyOffset,end)); terminated.push(true);
                consumeKeys(end-keyOffset+1); return;
            }
            prefix = keyQueue.slice(keyOffset); consumeKeys(keyQueue.length-keyOffset);
        }
        if (closed) fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
        return Promise.resolve(requestLine(line, column, 'line', prefix)).then(text => {
            if (text === null) {
                closed = true;
                if (prefix) {lines.push(prefix);terminated.push(false);return;}
                fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
            }
            if (typeof text !== 'string' || /[\r\n]/.test(text))
                fail('R_INPUT_INVALID', 'Expected one submitted input line.', line, column);
            inputBytes += Math.max(0,encoder.encode(text).length-encoder.encode(prefix).length) + 1;
            if (inputBytes > inputLimit) fail('R_INPUT_LIMIT', 'Program input exceeds the input limit.', line, column);
            lines.push(text); terminated.push(true);
        });
    }
    /* CRT keys consume buffered console input before asking the live provider.
     * Raw chunks have no synthetic delimiter or echo, and remain bounded/validated. */
    function readKey(line, column) {
        if (position < lines.length)
            return mapped(streamCharacter(0,line,column), value=>value === 10 ? 13 : value);
        if (keyQueue) {
            const value = keyQueue.codePointAt(keyOffset);
            consumeKeys(value > 0xffff ? 2 : 1); return value;
        }
        if (closed) fail('R_INPUT_EOF','Program input is exhausted.',line,column);
        return Promise.resolve(requestLine(line,column,'key','')).then(text=> {
            if (text === null) {closed = true;fail('R_INPUT_EOF','Program input is exhausted.',line,column);}
            if (typeof text !== 'string' || !text.length)
                fail('R_INPUT_INVALID','Expected a nonempty key chunk.',line,column);
            text = text.replace(/\r\n|\n/g,'\r');
            const value = inputValue(text,line,column);
            inputBytes += value.length;
            if (inputBytes > inputLimit) fail('R_INPUT_LIMIT','Program input exceeds the input limit.',line,column);
            keyQueue = text; keyOffset = 0;
            return readKey(line,column);
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
    function inputValue(text, line, column) {
        for (const scalar of text) {
            const value = scalar.codePointAt(0);
            if (value >= 0xd800 && value <= 0xdfff)
                fail('R_ENCODING', 'Input contains an invalid Unicode surrogate.', line, column);
        }
        return encoder.encode(text);
    }
    /* Ordinary and variant input share validation; destinations commit afterward. */
    function inputString(text, destination, capacity, line, column) {
        storeString(destination, capacity, inputValue(text, line, column), line, column);
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
    /* Read u32 metadata through the same checked program-memory boundary as values. */
    function word(pointer, line, column) {
        bytes(pointer, 4, line, column);
        return new DataView(memory.buffer).getUint32(pointer, true);
    }
    /* Requests borrow a bounded claim array and the current frame lease/control lists.
     * Variant leases have kind 1 at +12; heap leases retain kind 0 and their old layout. */
    function variantRequest(pointer, line, column) {
        bytes(pointer, 16, line, column);
        const count = word(pointer, line, column), leases = [], heapLeases = [], controls = [], claims = [];
        if (count > 128) fail('R_MEMORY', 'Variant claim limit exceeded.', line, column);
        bytes(pointer + 16, count * 16, line, column);
        const controlBase = word(pointer + 8, line, column);
        const controlCount = word(pointer + 12, line, column);
        if (controlCount > 8192) fail('R_MEMORY', 'Invalid counted-control list.', line, column);
        bytes(controlBase, controlCount * 4, line, column);
        for (let i = 0; i < controlCount; i++) controls.push(word(controlBase + i * 4, line, column));
        const stackBase = controlBase + 32768, stackEnd = stackBase + 131072;
        const leaseHead = word(pointer + 4, line, column);
        let head = leaseHead, steps = 0;
        while (head) {
            if (++steps > 8192 || head < stackBase || head > stackEnd - 16 || head % 16)
                fail('R_MEMORY', 'Invalid variant lease chain.', line, column);
            const kind = word(head + 12, line, column);
            if (kind > 1) fail('R_MEMORY', 'Invalid lease kind.', line, column);
            if (kind) leases.push({pointer:word(head, line, column), schema:word(head + 8, line, column)});
            else heapLeases.push(word(head, line, column));
            head = word(head + 4, line, column);
        }
        for (let i = 0; i < count; i++) {
            const row = pointer + 16 + i * 16;
            const address = word(row, line, column), schema = word(row + 4, line, column);
            const selected = word(row + 8, line, column), field = word(row + 12, line, column);
            if (schema === 0xffffffff) {
                if (selected || field) fail('R_MEMORY', 'Invalid address claim group.', line, column);
                if (address) claims.push(...liveReference(address, line, column).claims);
            } else claims.push({pointer:address, schema, selected, field});
            if (claims.length > 128) fail('R_MEMORY', 'Expanded variant claim limit exceeded.', line, column);
        }
        return {pointer, claims, leases, heapLeases, controls, controlBase, stackBase, stackEnd, leaseHead};
    }
    /* Decode immutable backend schemas. Each arm has fixed typed fields followed
     * by an optional nested variant region; schemas reside in the literal region. */
    function variantSchema(pointer, line, column) {
        if (variantSchemas.has(pointer)) return variantSchemas.get(pointer);
        const start = pointer, arms = [];
        const count = word(pointer, line, column);
        if (pointer >= 262144 || !count || count > 1024)
            fail('R_MEMORY', 'Invalid variant schema.', line, column);
        pointer += 4;
        let fields = 0;
        for (let i = 0; i < count; i++) {
            const n = word(pointer, line, column);
            const nested = word(pointer + 4, line, column), nestedSize = word(pointer + 8, line, column);
            const size = word(pointer + 12, line, column), members = [];
            if ((fields += n) > 4096 || size < 16 || size > 67108864 || size % 16 ||
                nested < 16 || nested > size || nestedSize !== size - nested)
                fail('R_MEMORY', 'Invalid variant arm layout.', line, column);
            pointer += 16;
            let expected = 16;
            for (let j = 0; j < n; j++) {
                const offset = word(pointer, line, column), length = word(pointer + 4, line, column);
                const type = word(pointer + 8, line, column);
                if (offset !== expected || !length || length % 16 || length > size - offset || !type)
                    fail('R_MEMORY', 'Invalid variant field layout.', line, column);
                members.push({offset, length, type});
                expected += length; pointer += 12;
            }
            if (expected !== nested) fail('R_MEMORY', 'Invalid variant prefix.', line, column);
            arms.push({members, byOffset:new Map(members.map(field => [field.offset, field])), nested, nestedSize, size});
        }
        if (pointer > 262144 || pointer < start || variantSchemaBytes + pointer - start > 262144)
            fail('R_MEMORY', 'Variant schema exceeds literal memory.', line, column);
        variantSchemaBytes += pointer - start;
        variantSchemas.set(start, arms);
        return arms;
    }
    /* Variant activation can clear an entire arm region, so its controls are checked
     * independently of the selected destination's ordinary leaf/aggregate guard. */
    function variantControls(request, pointer, length, line, column) {
        if (request.controls.some(control => control >= pointer && control < pointer + length))
            fail('R_FOR_CONTROL', 'An alias would modify an active counted control.', line, column);
    }
    /* Build a bounded mutation plan without touching memory. Virtual reads account
     * for earlier ancestor clears, preventing stale nested bytes from becoming tags. */
    function variantPlan(request, writable, line, column) {
        const operations = [];
        function activeAt(pointer) {
            for (let i = operations.length - 1; i >= 0; i--) {
                const operation = operations[i];
                if (operation.tag && operation.pointer === pointer) return operation.tag;
                if (!operation.tag && pointer >= operation.pointer && pointer + 4 <= operation.pointer + operation.length)
                    return 0;
            }
            return word(pointer, line, column);
        }
        for (const claim of request.claims) {
            const {pointer, selected, field} = claim;
            const arms = variantSchema(claim.schema, line, column);
            if (!selected || selected > arms.length || (field !== 0xffffffff && field >= arms[selected - 1].members.length))
                fail('R_MEMORY', 'Invalid variant view.', line, column);
            const size = Math.max(...arms.map(arm => arm.size));
            bytes(pointer, size, line, column);
            const active = activeAt(pointer);
            if (active > arms.length) fail('R_MEMORY', 'Invalid variant storage state.', line, column);
            const incoming = arms[selected - 1], old = arms[active - 1];
            const member = incoming.members[field];
            const previous = old && member && old.byOffset.get(member.offset);
            const compatible = active === selected || (previous &&
                previous.length === member.length && previous.type === member.type);
            if (compatible) continue;
            if (!writable) fail(active ? 'R_VARIANT' : 'R_UNINITIALIZED',
                active ? 'Variant field has an incompatible active storage view.' : 'Variant field was read before initialization.', line, column);
            variantControls(request, pointer, size, line, column);
            if (request.leases.some(lease => lease.pointer === pointer))
                fail('R_LIFETIME', 'Cannot change a variant arm while it is borrowed.', line, column);
            for (const next of incoming.members) {
                const previous = old && old.byOffset.get(next.offset);
                if (!previous || previous.length !== next.length || previous.type !== next.type)
                    operations.push({pointer:pointer + next.offset, length:next.length});
            }
            if (incoming.nestedSize) operations.push({pointer:pointer + incoming.nested, length:incoming.nestedSize});
            operations.push({pointer, tag:selected});
            if (operations.length > 65536) fail('R_MEMORY', 'Variant activation exceeds its work budget.', line, column);
        }
        return operations;
    }
    /* All validation, source snapshots and lease checks precede this infallible commit. */
    function applyVariantPlan(operations) {
        const view = new DataView(memory.buffer);
        for (const operation of operations) {
            if (operation.tag) view.setUint32(operation.pointer, operation.tag, true);
            else new Uint8Array(memory.buffer, operation.pointer, operation.length).fill(0);
        }
    }
    /* A whole value may contain nested variant headers. Leases stay outside copied
     * values; only a change to a live header is forbidden, not compatible field writes. */
    function variantCopy(destination, source, size, capacity, pointer, line, column) {
        const request = variantRequest(pointer, line, column);
        const value = size ? bytes(source, size, line, column).slice() : stringBytes(source, line, column).slice();
        commitVariantValue(destination,value,size,capacity,request,line,column);
    }
    /* Both memory copies and typed-file snapshots use the same atomic guard plan. */
    function commitVariantValue(destination, value, size, capacity, request, line, column) {
        const length = size || 16 + capacity;
        bytes(destination, length, line, column);
        if (!size && (capacity < 0 || capacity > 1048576 || value.length > capacity))
            fail(value.length > capacity ? 'R_STRING_CAPACITY' : 'R_MEMORY', 'Invalid variant string capacity.', line, column);
        variantControls(request, destination, length, line, column);
        const operations = variantPlan(request, true, line, column);
        if (size) {
            const incoming = new DataView(value.buffer, value.byteOffset, value.byteLength);
            for (const lease of request.leases) {
                if (lease.pointer < destination || lease.pointer > destination + size - 4) continue;
                const old = word(lease.pointer, line, column);
                const next = incoming.getUint32(lease.pointer - destination, true);
                if (old === next) continue;
                const arms = variantSchema(lease.schema, line, column), a = arms[old - 1], b = arms[next - 1];
                // Identical fixed layouts preserve every borrowed view despite the new arm ID.
                const compatible = a && b && !a.nestedSize && !b.nestedSize && a.size === b.size &&
                    a.members.length === b.members.length && a.members.every((field, i) =>
                        field.offset === b.members[i].offset && field.length === b.members[i].length &&
                        field.type === b.members[i].type);
                if (!compatible) fail('R_LIFETIME', 'Cannot replace a borrowed variant arm.', line, column);
            }
        }
        applyVariantPlan(operations);
        if (size) bytes(destination, size, line, column).set(value);
        else storeString(destination, capacity, value, line, column);
    }
    /* Heap ownership comes from a live backend lease, never from guessing headers
     * at arbitrary interior offsets. Validate generation again on every later use. */
    function heapOwner(header, line, column) {
        bytes(header, 32, line, column);
        const size = word(header + 20, line, column), generation = word(header + 8, line, column);
        if (header % 16 || !generation || word(header + 12, line, column) !== 1 ||
            !size || size % 16 || size > word(header + 4, line, column))
            fail('R_DANGLING', 'Address owner is no longer a live allocation.', line, column);
        bytes(header + 32, size, line, column);
        return {kind:'heap', base:header + 32, end:header + 32 + size, header, generation, size};
    }
    /* Registry entries retain lifetimes without retaining heap or variant leases. */
    function referenceAlive(entry) {
        const owner = entry.owner;
        if (owner.kind === 'global') return true;
        if (owner.kind === 'frame') return frames.get(owner.base)?.epoch === owner.epoch;
        return word(owner.header + 8) === owner.generation && word(owner.header + 12) === 1 &&
            word(owner.header + 20) === owner.size;
    }
    /* A missing ID and an expired owner have the same checked dangling diagnostic. */
    function liveReference(id, line, column) {
        const entry = references.get(id);
        if (!entry || !referenceAlive(entry))
            fail('R_DANGLING', 'Address refers to storage whose lifetime has ended.', line, column);
        return entry;
    }
    /* Removal never reuses an ID, even when a later frame/allocation has the same bytes. */
    function removeReference(id, entry) {
        references.delete(id); referenceKeys.delete(entry.key);
        if (entry.owner.kind === 'frame') entry.owner.identities.delete(id);
    }
    /* Infer the actual owner of a captured var/with/heap alias, preserving its root. */
    function addressOwner(target, length, root, request, line, column) {
        let owner;
        if (target >= 262144 && target + length <= request.controlBase)
            owner = {kind:'global', base:262144, end:request.controlBase};
        if (!owner) for (const frame of frames.values()) {
            if (target >= frame.base + 16 && target + length <= frame.end) {
                owner = frame; break;
            }
        }
        if (!owner) for (const header of request.heapLeases) {
            const candidate = heapOwner(header, line, column);
            if (target >= candidate.base && target + length <= candidate.end) {
                owner = candidate; break;
            }
        }
        if (!owner || root < owner.base || root + 16 > owner.end)
            fail('R_POINTER', 'Address does not belong to live writable storage.', line, column);
        return owner;
    }
    /* Canonical field signatures make compatible overlapping aliases equal. Exact
     * ancestor claims remain distinct; they protect the containing logical view. */
    function addressClaims(request, target, length, line, column) {
        const claims = request.claims.slice();
        // A var/with binding can hide the syntactic path; its leases retain ancestors.
        for (const lease of request.leases) {
            const arms = variantSchema(lease.schema, line, column);
            const selected = word(lease.pointer, line, column), arm = arms[selected - 1];
            if (!arm) fail('R_MEMORY', 'Invalid borrowed variant view.', line, column);
            const field = arm.members.findIndex(member => target >= lease.pointer + member.offset &&
                target + length <= lease.pointer + member.offset + member.length);
            if (field >= 0) claims.push({...lease, selected, field});
            else if (arm.nestedSize && target >= lease.pointer + arm.nested &&
                target + length <= lease.pointer + arm.size)
                claims.push({...lease, selected, field:0xffffffff});
        }
        const canonical = new Map();
        for (const claim of claims) {
            const arms = variantSchema(claim.schema, line, column);
            const member = arms[claim.selected - 1]?.members[claim.field];
            let selected = claim.selected, field = claim.field;
            if (field !== 0xffffffff) {
                if (!member) fail('R_MEMORY', 'Invalid address variant field.', line, column);
                for (let i = 0; i < arms.length; i++) {
                    const match = arms[i].members.findIndex(other => other.offset === member.offset &&
                        other.length === member.length && other.type === member.type);
                    if (match >= 0) { selected = i + 1; field = match; break; }
                }
            } else if (!arms[selected - 1]) fail('R_MEMORY', 'Invalid address ancestor.', line, column);
            const key = `${claim.pointer}:${claim.schema}:${selected}:${field}`;
            canonical.set(key, {pointer:claim.pointer, schema:claim.schema, selected, field});
        }
        const result = [...canonical.values()].sort((a,b) => a.pointer - b.pointer || a.schema - b.schema);
        if (result.length > 128) fail('R_MEMORY', 'Address view exceeds its claim budget.', line, column);
        return result;
    }
    /* Capture coordinates, not payloads. Character descriptors are transient, so
     * only their stable string source and Unicode scalar index enter the registry. */
    function addressOf(target, root, size, type, pointer, line, column) {
        if (!size || size % 16 || !type) fail('R_MEMORY', 'Invalid address type.', line, column);
        const request = variantRequest(pointer, line, column);
        let source = target, length = size, index = 0;
        if (target < 0) {
            const character = characterView(target, line, column);
            source = character.source; index = character.index;
            stringCharacters(source, index, line, column);
            length = Math.ceil((16 + word(source + 4, line, column)) / 16) * 16;
        }
        const owner = addressOwner(source, length, root, request, line, column);
        const claims = addressClaims(request, source, length, line, column);
        // Complete heap-object addresses preserve allocation identity and disposal.
        if (!index && owner.kind === 'heap' && source === owner.base && size === owner.size && !claims.length)
            return (BigInt(owner.generation) << 32n) | BigInt(source);
        const lifetime = owner.kind === 'frame' ? owner.epoch : owner.generation || 0;
        const key = JSON.stringify([owner.kind, owner.base, lifetime, source, index, root, size, type, claims]);
        const previous = referenceKeys.get(key);
        if (previous !== undefined) return (BigInt(previous) << 32n) | 0x80000000n;
        if (references.size >= referenceLimit) {
            for (const [id, entry] of references) if (!referenceAlive(entry)) removeReference(id, entry);
        }
        if (references.size >= referenceLimit || referenceSequence === 0xffffffff)
            fail('R_MEMORY', 'Address identity budget exhausted.', line, column);
        const id = ++referenceSequence;
        const entry = {key, owner, source, index, root, size, type, claims};
        references.set(id, entry); referenceKeys.set(key, id);
        if (owner.kind === 'frame') owner.identities.add(id);
        return (BigInt(id) << 32n) | 0x80000000n;
    }
    /* A resolved reference returns a private 48-byte packet. The compiler borrows
     * its heap owner before evaluating more Pascal code and expands its view claims. */
    function addressView(token, id, size, result, line, column) {
        if ((token >>> 0) !== 0x80000000) fail('R_POINTER', 'Invalid address token.', line, column);
        const entry = liveReference(id >>> 0, line, column);
        if (entry.size !== size) fail('R_POINTER', 'Address has an incompatible storage size.', line, column);
        bytes(result, 48, line, column);
        const view = new DataView(memory.buffer);
        let target = entry.source;
        if (entry.index) {
            target = (result + 32) | 0x80000000;
            view.setUint32(result + 32, entry.source, true);
            view.setUint32(result + 36, entry.index, true);
        }
        const owner = entry.owner;
        [target,entry.root,owner.kind === 'heap' ? owner.base : 0,owner.generation || 0,
            owner.size || 0,id >>> 0,0,0].forEach((value,i) => view.setUint32(result + i * 4,value,true));
    }
    /* Bind may expand a reference group to several ancestors. Snapshot and preflight
     * the complete request before reusing its temporary bytes as kind-1 leases. */
    function variantBind(pointer, base, end, line, column) {
        const request = variantRequest(pointer, line, column);
        const claims = [...new Map(request.claims.map(claim =>
            [`${claim.pointer}:${claim.schema}`,claim])).values()];
        if (base % 16 || base < request.stackBase || end !== request.stackEnd ||
            base > end - claims.length * 16)
            fail('R_MEMORY', 'Variant binding exceeds frame storage.', line, column);
        bytes(base, claims.length * 16, line, column);
        const operations = variantPlan(request, true, line, column);
        applyVariantPlan(operations);
        const view = new DataView(memory.buffer);
        let head = request.leaseHead;
        for (let i = 0; i < claims.length; i++) {
            const address = base + i * 16, claim = claims[i];
            [claim.pointer,head,claim.schema,1].forEach((value,j) => view.setUint32(address + j*4,value,true));
            head = address;
        }
        return (BigInt(head) << 32n) | BigInt(base + claims.length * 16);
    }
    /* Preflight variable-width replacement against the virtual activation plan.
     * A cleared string header cannot be read from the incompatible arm's old bytes. */
    function prepareCharacterStore(target, value, operations, line, column) {
        const text = character(value, line, column);
        if (target < 0) {
            const view = characterView(target, line, column);
            if (operations.some(operation => !operation.tag && view.source + 8 >= operation.pointer &&
                view.source + 8 < operation.pointer + operation.length))
                fail('R_UNINITIALIZED', 'String character view would lose its initialized owner.', line, column);
            const characters = stringCharacters(view.source, view.index, line, column);
            characters[view.index - 1] = text;
            const capacity = word(view.source + 4, line, column);
            const value = encoder.encode(characters.join(''));
            bytes(view.source,16 + capacity,line,column);
            if (value.length > capacity)
                fail('R_STRING_CAPACITY', 'String exceeds its UTF-8 byte capacity.', line, column);
            return {source:view.source, capacity, bytes:value};
        }
        bytes(target,16,line,column);
        return {target, value};
    }
    /* All ranges, scalar values and capacity are already validated; activation may
     * now clear other fields before this synchronous, infallible payload commit. */
    function commitCharacterStore(plan) {
        if (plan.bytes) storeString(plan.source,plan.capacity,plan.bytes);
        else {
            const view = new DataView(memory.buffer,plan.target,16);
            view.setInt32(0,plan.value,true); view.setUint8(8,1);
        }
    }
    /* Close without checks only during validated close/owner retirement/finalization.
     * Named bytes survive close; dropping a binding never removes their contents. */
    function closeFile(binding) {
        if (!binding.file) return;
        if (binding.mode === 0) binding.file.readers.delete(binding.id);
        else binding.file.writer = 0;
        binding.file = null; binding.mode = null; binding.cursor = null; openFiles--;
    }
    /* Descriptor IDs never repeat within an attachment, including reassignment. */
    function dropFileBinding(binding) {
        closeFile(binding); fileBindings.delete(binding.id);
        if (binding.owner.kind === 'frame') binding.owner.files.delete(binding.id);
    }
    /* Completion/failure discards all run-owned bytes and handles, without touching
     * Pascal memory used by diagnostic/atomicity assertions. Stop drops the worker. */
    function finishFiles() {
        for (const binding of fileBindings.values()) dropFileBinding(binding);
        virtualFiles.clear(); fileBytes = 0; openFiles = 0;
    }
    /* Successful main completion transfers bounded text snapshots out of the live
     * filesystem. Unchanged inputs and internal typed snapshots are not results. */
    function completeFiles() {
        fileResults = [];
        for (const [name, file] of virtualFiles)
            if (file.kind && file.generated)
                fileResults.push({name, data:file.data.slice(0, file.length)});
        fileLoadAllowed = false; finishFiles();
    }
    /* Seed one fresh attachment atomically. Preflight all metadata before copying
     * at most 1 MiB, then validate the owned bytes without changing their encoding. */
    function loadFiles(files) {
        if (!fileLoadAllowed)
            fail('R_FILE_STATE', 'Input files require a fresh attachment before execution.');
        if (!Array.isArray(files)) fail('R_FILE_TYPE', 'Input files must be a list.');
        if (files.length > 32) fail('R_FILE_LIMIT', 'Input files exceed the 32-file limit.');
        const names = new Set(); let total = 0;
        for (const file of files) {
            const name = file?.name;
            if (typeof name !== 'string' || !name || name.length > 255 || name.includes('\0'))
                fail('R_FILE_NAME', 'Input filenames require 1..255 UTF-8 bytes without NUL.');
            for (const scalar of name) {
                const value = scalar.codePointAt(0);
                if (value >= 0xd800 && value <= 0xdfff)
                    fail('R_FILE_NAME', 'Input filename contains invalid Unicode.');
            }
            if (encoder.encode(name).length > 255 || names.has(name))
                fail('R_FILE_NAME', 'Input filename is too long or duplicated.');
            names.add(name);
            if (!(file.data instanceof Uint8Array))
                fail('R_FILE_TYPE', 'Input file contents require a Uint8Array.');
            total += file.data.byteLength;
            if (total > 1048576) fail('R_FILE_LIMIT', 'Input files exceed 1 MiB.');
        }
        const prepared = files.map(file => ({name:file.name, data:new Uint8Array(file.data)}));
        for (const file of prepared) {
            try { decoder.decode(file.data); }
            catch { fail('R_ENCODING', 'Input text file is not valid UTF-8.'); }
        }
        for (const file of prepared)
            virtualFiles.set(file.name, {kind:1,type:0,size:0,data:file.data,length:file.data.length,
                readers:new Set(),writer:0,text:null,generated:false});
        fileBytes = total; fileLoadAllowed = false;
    }
    /* Abandoning a worker/embedding releases accepted typeahead as well as files.
     * File-only finish stays separate so virtual I/O never changes console state. */
    function finishRuntime() {
        finishFiles(); fileResults = []; fileLoadAllowed = false; keyQueue = ''; keyOffset = 0;
        lines = []; terminated = []; position = offset = 0; closed = true;
    }
    /* Stale heap owners release their bindings before any capacity/open-mode check.
     * Frames retire their own small sets directly, avoiding scans on routine return. */
    function collectFiles() {
        for (const binding of fileBindings.values())
            if (!referenceAlive(binding)) dropFileBinding(binding);
    }
    /* A descriptor is noncopyable. Both ID and cell coordinate must match, so stale
     * initialized bytes cannot alias a newly assigned, unrelated file binding. */
    function fileBinding(target, line, column) {
        const object = bytes(target,16,line,column);
        if (!object[8]) fail('R_UNINITIALIZED','File descriptor was used before assign.',line,column);
        const binding = fileBindings.get(word(target,line,column));
        if (binding && !referenceAlive(binding)) dropFileBinding(binding);
        if (!binding || binding.target !== target || !fileBindings.has(binding.id))
            fail('R_FILE_STATE','File descriptor is no longer assigned.',line,column);
        return binding;
    }
    /* Begin/end freeze the opened binding while operand calls evaluate. Nested I/O
     * can borrow it; close/reopen/reassign through any alias cannot invalidate it. */
    function fileUse(id, mode, line, column) {
        const binding = fileBindings.get(id >>> 0);
        if (binding && !referenceAlive(binding)) dropFileBinding(binding);
        if (!binding || !binding.file || binding.mode !== mode || !binding.borrowers)
            fail('R_FILE_STATE','Virtual file is not open in the required mode.',line,column);
        return binding;
    }
    /* Validate the filename, lifetime and budgets before replacing a closed binding. */
    function assignFile(target, root, kind, type, size, name, pointer, line, column) {
        fileLoadAllowed = false;
        if (target % 16 || root % 16) fail('R_POINTER','Unaligned file descriptor.',line,column);
        if (!Number.isInteger(type) || !Number.isInteger(size) || (kind !== 0 && kind !== 1) ||
            (kind === 1 ? type || size : type < 1 || size < 16 || size > 67108864 || size % 16))
            fail('R_FILE_TYPE','Invalid virtual file component.',line,column);
        const encoded = stringBytes(name,line,column), filename = stringText(name,line,column);
        if (!encoded.length || encoded.length > 255 || filename.includes('\0'))
            fail('R_FILE_NAME','Virtual filenames require 1..255 UTF-8 bytes without NUL.',line,column);
        const request = variantRequest(pointer,line,column);
        const owner = addressOwner(target,16,root,request,line,column);
        collectFiles();
        const object = bytes(target,16,line,column);
        const previous = object[8] ? fileBindings.get(word(target,line,column)) : null;
        if (object[8] && (!previous || previous.target !== target) || previous?.file || previous?.borrowers)
            fail('R_FILE_STATE','Cannot reassign an open or invalid descriptor.',line,column);
        if (fileBindings.size - (previous ? 1 : 0) >= 256 || fileSequence === 0xffffffff)
            fail('R_FILE_LIMIT','Assigned descriptor budget exhausted.',line,column);
        const operations = variantPlan(request,true,line,column);
        variantControls(request,target,16,line,column);
        const id = ++fileSequence;
        const binding = {id,target,owner,name:filename,kind,type,size,file:null,mode:null,cursor:null,borrowers:0};
        applyVariantPlan(operations);
        if (previous) dropFileBinding(previous);
        fileBindings.set(id,binding);
        if (owner.kind === 'frame') owner.files.add(id);
        object.fill(0); new DataView(memory.buffer).setUint32(target,id,true); object[8] = 1;
    }
    /* Open modes are exclusive for writers and shared for readers. Rewrite may
     * replace kind/component metadata only when no other handle retains old data. */
    function controlFile(target, operation, line, column) {
        collectFiles();
        const binding = fileBinding(target,line,column);
        if (binding.borrowers) fail('R_FILE_STATE','Virtual file is in use by an I/O operation.',line,column);
        if (operation === 2) {
            if (!binding.file) fail('R_FILE_STATE','Virtual file is already closed.',line,column);
            closeFile(binding); return;
        }
        if (binding.file || (operation !== 0 && operation !== 1))
            fail('R_FILE_STATE','Cannot reopen an open descriptor.',line,column);
        if (openFiles >= 32) fail('R_FILE_LIMIT','Open virtual file budget exhausted.',line,column);
        let file = virtualFiles.get(binding.name);
        if (operation === 0) {
            if (file && (file.writer || file.readers.size))
                fail('R_FILE_STATE','Cannot truncate a virtual file while it is open.',line,column);
            if (!file && virtualFiles.size >= 32)
                fail('R_FILE_LIMIT','Named virtual file budget exhausted.',line,column);
            const oldLength = file?.length || 0;
            file = {kind:binding.kind,type:binding.type,size:binding.size,data:new Uint8Array(0),
                length:0,readers:new Set(),writer:binding.id,text:null,generated:true};
            virtualFiles.set(binding.name,file); fileBytes -= oldLength;
            binding.mode = 1; binding.cursor = 0;
        } else {
            if (!file) fail('R_FILE_NOT_FOUND','Virtual file does not exist in this execution.',line,column);
            if (file.writer) fail('R_FILE_STATE','Virtual file has a live writer.',line,column);
            if (file.kind !== binding.kind || file.type !== binding.type || file.size !== binding.size)
                fail('R_FILE_TYPE','Virtual file contents have an incompatible component type.',line,column);
            let cursor = 0;
            if (file.kind) {
                if (!file.text) {
                    let text;
                    try {text = decoder.decode(file.data.subarray(0,file.length));}
                    catch {fail('R_ENCODING','Virtual text file is not valid UTF-8.',line,column);}
                    const lines = text === '' ? [] : text.split(/\r?\n/);
                    if (text.endsWith('\n')) lines.pop();
                    file.text = {lines,terminated:lines.map((_,i)=>i < lines.length-1 || text.endsWith('\n'))};
                }
                cursor = {...file.text,position:0,offset:0};
            }
            file.readers.add(binding.id); binding.mode = 0; binding.cursor = cursor;
        }
        binding.file = file; openFiles++;
    }
    /* Capacity grows geometrically under the cumulative byte limit. All file data
     * capacities together are below twice that limit plus small empty-file overhead. */
    function appendFile(binding, value, line, column) {
        const file = binding.file;
        if (value.length > 1048576 - fileBytes)
            fail('R_FILE_LIMIT','Virtual file contents exceed 1 MiB.',line,column);
        const end = file.length + value.length;
        if (end > file.data.length) {
            let capacity = Math.max(16,file.data.length);
            while (capacity < end) capacity *= 2;
            const data = new Uint8Array(capacity); data.set(file.data.subarray(0,file.length));
            file.data = data;
        }
        file.data.set(value,file.length); file.length = end; fileBytes += value.length; file.text = null;
    }
    /* A final unterminated line has no delimiter left after its last scalar. */
    function fileTextEnd(cursor) {
        return cursor.position >= cursor.lines.length ||
            (cursor.position === cursor.lines.length-1 && cursor.offset === cursor.lines[cursor.position].length &&
             !cursor.terminated[cursor.position]);
    }
    /* File contents are already present and immutable while readers are open.
     * closed=true guarantees shared parsers never suspend or ask for console input. */
    function withFileCursor(id, line, column, action) {
        const binding = fileUse(id,0,line,column);
        if (!binding.kind) fail('R_FILE_TYPE','Expected a virtual text file.',line,column);
        const saved = {lines,terminated,position,offset,closed}, cursor = binding.cursor;
        lines = cursor.lines; terminated = cursor.terminated;
        position = cursor.position; offset = cursor.offset; closed = true;
        try {return action();}
        catch (error) {
            if (error.code === 'R_INPUT_EOF') {
                error.code = 'R_FILE_EOF'; error.message = 'Virtual file input is exhausted.';
            }
            throw error;
        } finally {
            cursor.position = position; cursor.offset = offset;
            ({lines,terminated,position,offset,closed} = saved);
        }
    }
    /* ABI record: name/signature pairs are documented in docs/runtime-abi.md. */
    const imports = {
        key_i32:readKey,
        file_assign:assignFile,
        file_control:controlFile,
        file_begin(target, mode, line, column) {
            const binding = fileBinding(target,line,column);
            if (!binding.file || binding.mode !== mode || (mode !== 0 && mode !== 1))
                fail('R_FILE_STATE','Virtual file is not open in the required mode.',line,column);
            if (binding.borrowers >= 8192) fail('R_FILE_LIMIT','Virtual file borrow budget exhausted.',line,column);
            binding.borrowers++; return binding.id;
        },
        file_end(id, line, column) {
            const binding = fileBindings.get(id >>> 0);
            if (!binding || !binding.borrowers) fail('R_FILE_STATE','Invalid virtual file borrow.',line,column);
            binding.borrowers--;
        },
        file_query(target, query, line, column) {
            const binding = fileBinding(target,line,column), file = binding.file;
            if (!file) fail('R_FILE_STATE','Virtual file is closed.',line,column);
            if (query === 2) {
                if (binding.kind) fail('R_FILE_TYPE','filesize requires a typed file.',line,column);
                return file.length / binding.size;
            }
            if (binding.mode !== 0 || (query !== 0 && query !== 1))
                fail('R_FILE_STATE','Virtual file is not open for this input query.',line,column);
            if (query === 1 && !binding.kind) fail('R_FILE_TYPE','eoln requires a text file.',line,column);
            if (!binding.kind) return Number(binding.cursor >= file.length);
            return Number(fileTextEnd(binding.cursor) || (query === 1 &&
                binding.cursor.offset === binding.cursor.lines[binding.cursor.position].length));
        },
        file_write(id, source, size, line, column) {
            const binding = fileUse(id,1,line,column);
            if (binding.kind || binding.size !== size) fail('R_FILE_TYPE','Invalid typed file write.',line,column);
            if (size > 1048576 - fileBytes) fail('R_FILE_LIMIT','Virtual file contents exceed 1 MiB.',line,column);
            appendFile(binding,bytes(source,size,line,column),line,column);
        },
        file_read(id, destination, size, pointer, line, column) {
            const binding = fileUse(id,0,line,column);
            if (binding.kind || binding.size !== size) fail('R_FILE_TYPE','Invalid typed file read.',line,column);
            if (binding.cursor >= binding.file.length) fail('R_FILE_EOF','Virtual file input is exhausted.',line,column);
            const value = binding.file.data.slice(binding.cursor,binding.cursor+size);
            const request = variantRequest(pointer,line,column);
            if (destination < 0) {
                if (size !== 16 || !value[8]) fail('R_UNINITIALIZED','File character is uninitialized.',line,column);
                const operations = variantPlan(request,true,line,column);
                const character = prepareCharacterStore(destination,new DataView(value.buffer).getInt32(0,true),operations,line,column);
                applyVariantPlan(operations); commitCharacterStore(character);
            } else commitVariantValue(destination,value,size,0,request,line,column);
            binding.cursor += size;
        },
        file_text_write(id, kind, value, width, precision, line, column) {
            const binding = fileUse(id,1,line,column);
            if (!binding.kind) fail('R_FILE_TYPE','Expected a virtual text file.',line,column);
            const text = kind === 6 ? '\n' : formattedValue(kind,value,width,precision,line,column,
                1048576-fileBytes,'R_FILE_LIMIT');
            appendFile(binding,encoder.encode(text),line,column);
        },
        file_text_i32(id, mode, line, column) {
            return withFileCursor(id,line,column,()=>streamNumber(mode,false,line,column));
        },
        file_text_f64(id, mode, line, column) {
            return withFileCursor(id,line,column,()=>streamNumber(mode,true,line,column));
        },
        file_text_char(id, mode, line, column) {
            return withFileCursor(id,line,column,()=>streamCharacter(mode,line,column));
        },
        file_text_string(id, destination, capacity, mode, pointer, line, column) {
            return withFileCursor(id,line,column,()=> {
                ensureLine(line,column);
                if (offset > 0 && offset === lines[position].length && !terminated[position])
                    fail('R_INPUT_EOF','Virtual file input is exhausted.',line,column);
                const value = inputValue(lines[position].slice(offset),line,column);
                commitVariantValue(destination,value,0,capacity,variantRequest(pointer,line,column),line,column);
                offset = lines[position].length;
            });
        },
        file_text_discard(id, line, column) {
            return withFileCursor(id,line,column,()=>nextLine(line,column));
        },
        file_finish:completeFiles,
        address_of:addressOf,
        address_view:addressView,
        variant_bind:variantBind,
        /* Frame epochs are monotonic private Wasm state, independent of reusable
         * arena coordinates. Retire only references owned by this activation. */
        frame_enter(base, size, epoch, line, column) {
            epoch >>>= 0;
            if (!epoch || base % 16 || size < 16 || size % 16 || frames.size >= 65 ||
                frames.has(base) || word(base + 12, line, column) !== epoch)
                fail('R_MEMORY', 'Invalid address frame registration.', line, column);
            bytes(base, size, line, column);
            if ([...frames.values()].some(frame => base < frame.end && base + size > frame.base))
                fail('R_MEMORY', 'Address frames overlap.', line, column);
            frames.set(base, {kind:'frame', base, end:base + size, epoch, identities:new Set(), files:new Set()});
        },
        frame_leave(base, epoch, line, column) {
            const frame = frames.get(base);
            if (frame?.epoch !== (epoch >>> 0))
                fail('R_MEMORY', 'Invalid address frame retirement.', line, column);
            for (const id of frame.files) dropFileBinding(fileBindings.get(id));
            frames.delete(base);
            for (const id of frame.identities) removeReference(id, references.get(id));
        },
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
        /* Kinds and the f64 transport for exact i32/scalar/string addresses are ABI-defined. */
        write_formatted(kind, value, width, precision, line, column) {
            write(formattedValue(kind, value, width, precision, line, column));
        },
        /* Fresh state belongs to this runtime; explicit seeds make tests reproducible. */
        randomize() {
            randomState = (configuredSeed === undefined
                ? Math.floor(Math.random() * 0x100000000) : configuredSeed) || 1;
        },
        /* Bounds fail before advancing the stream; each successful call advances it. */
        random_i32(bound, line, column) { return randomInteger(bound, line, column); },
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
            commitCharacterStore(prepareCharacterStore(target,value,[],line,column));
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
        /* Memory-only variant services consume backend schemas and frame-owned requests. */
        variant_access(pointer, writable, line, column) {
            const request = variantRequest(pointer, line, column);
            const operations = variantPlan(request, writable !== 0, line, column);
            if (writable) applyVariantPlan(operations);
        },
        variant_char_store(target, value, pointer, line, column) {
            const request = variantRequest(pointer,line,column);
            if (target >= 0) variantControls(request,target,16,line,column);
            const operations = variantPlan(request,true,line,column);
            const character = prepareCharacterStore(target,value,operations,line,column);
            applyVariantPlan(operations);
            commitCharacterStore(character);
        },
        variant_copy(destination, source, size, capacity, pointer, line, column) {
            variantCopy(destination, source, size, capacity, pointer, line, column);
        },
        /* Validate supplied text before any arm activation, retaining the same JSPI cursor. */
        variant_read_string(destination, capacity, mode, pointer, line, column) {
            return mapped(ensureLine(line, column), () => {
                if (offset > 0 && offset === lines[position].length && !terminated[position])
                    fail('R_INPUT_EOF', 'Program input is exhausted.', line, column);
                const value = inputValue(lines[position].slice(offset), line, column);
                bytes(destination, 16 + capacity, line, column);
                if (capacity < 0 || capacity > 1048576 || value.length > capacity)
                    fail(value.length > capacity ? 'R_STRING_CAPACITY' : 'R_MEMORY', 'Invalid variant string capacity.', line, column);
                const request = variantRequest(pointer, line, column);
                variantControls(request, destination, 16 + capacity, line, column);
                const operations = variantPlan(request, true, line, column);
                applyVariantPlan(operations);
                storeString(destination, capacity, value, line, column);
                offset = lines[position].length;
            });
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
                18: ['R_POINTER', 'Invalid internal pointer or allocation bounds.'],
                19: ['R_RANGE', 'Value is outside its declared subrange.'],
                20: ['R_SET_RANGE', 'Set member is outside its declared domain.'],
                21: ['R_VARIANT', 'Variant field has an incompatible active storage view.']
            };
            const entry = errors[code] || ['R_FAILURE', 'Unknown program runtime failure.'];
            fail(entry[0], entry[1], line, column);
        }
    };
    return {imports, finish:finishRuntime, loadFiles,
        /* Consume results once. The caller owns their buffers independently of finish. */
        takeFiles() { const result = fileResults; fileResults = []; return result; },
        /* Bind only the freshly instantiated generated program's bounded memory. */
        attach(instance) {
            finishFiles(); fileResults = []; fileLoadAllowed = false; fileSequence = 0;
            memory = instance.exports.memory;
            variantSchemas.clear(); variantSchemaBytes = 0;
            references.clear(); referenceKeys.clear(); frames.clear(); referenceSequence = 0;
            keyQueue = ''; keyOffset = 0;
            if (!(memory instanceof WebAssembly.Memory) || memory.buffer.byteLength > 67108864)
                fail('R_MEMORY', 'Program memory exceeds 64 MiB or is missing.');
            fileLoadAllowed = true;
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
        /* Kind/prefix extend the line protocol without changing run/request guards. */
        function requestLine(line, column, kind = 'line', prefix = '') {
            flush();
            return new Promise(resolve => {
                waiting = {id:++sequence, resolve};
                postMessage({type:'input-request', id:runId, requestId:waiting.id, line, column, kind, prefix});
            });
        }
        let runtime;
        try {
            const jspi = supported();
            postMessage({type:'capabilities', id:runId, jspi});
            if (data.interactive && !jspi)
                throw Object.assign(new Error('Interactive console requires browser JSPI support. Choose Batch input.'), {code:'E_JSPI'});
            runtime = createPas2WasmRuntime(data.input || '', text => {
                pending += text;
                if (firstOutput || pending.length >= 4096 || performance.now() - flushedAt >= 16) {
                    flush(); firstOutput = false;
                }
            }, data.limits, data.interactive ? requestLine : null);
            const imports = {...runtime.imports};
            if (data.interactive) {
                for (const name of ['read_i32','read_f64','read_char','string_read','discard_line',
                    'stream_i32','stream_f64','stream_char','stream_string','variant_read_string','key_i32'])
                    imports[name] = new WebAssembly.Suspending(imports[name]);
            }
            const {instance} = await WebAssembly.instantiate(data.binary, {pas2wasm_v1:imports});
            runtime.attach(instance);
            runtime.loadFiles(data.files === undefined ? [] : data.files);
            postMessage({type:'running', id:runId});
            if (data.interactive) await WebAssembly.promising(instance.exports.run)();
            else instance.exports.run();
            const files = runtime.takeFiles();
            flush(); postMessage({type:'done', id:runId, files}, files.map(file => file.data.buffer));
        } catch (error) {
            flush(); postMessage({type:'error', id:runId, diagnostic:{
                code:error.code || 'R_TRAP', message:error.message,
                line:error.line || 0, column:error.column || 0
            }});
        } finally { runtime?.finish(); waiting = null; }
    };
}

/* Build the embedded worker without imports, network requests, or shared memory. */
function pas2WasmWorkerSource() {
    return `const createPas2WasmRuntime = ${createPas2WasmRuntime.toString()};\n(${pas2WasmProgramWorker.toString()})();`;
}
if (typeof module !== 'undefined' && module.exports)
    module.exports = {createPas2WasmRuntime, pas2WasmWorkerSource};
