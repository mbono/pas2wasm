/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {spawnSync} = require('node:child_process');
const cases = require('./cases.js');
const nativeExecutable = process.env.P2W_NATIVE || './build/pas2wasm';
const createCompiler = require('../web/compiler.js');
const {createPas2WasmRuntime} = require('../web/runtime/host.js');

/* Execute bounded acceptance programs with the same production runtime as browsers. */
async function execute(bytes, input = '', randomSeed) {
    let output = '';
    const host = createPas2WasmRuntime(input, text => { output += text; }, {randomSeed});
    const {instance} = await WebAssembly.instantiate(bytes, {pas2wasm_v1: host.imports});
    host.attach(instance);
    assert.equal(output, '', 'Instantiation must not execute the program');
    assert.throws(() => instance.exports.memory.grow(1024), RangeError);
    try { instance.exports.run(); return {output}; }
    catch (error) { return {output, code: error.code, line: error.line, column: error.column}; }
}

/* Observe the published first-global cell after failed narrowing: no payload or
 * initialization flag may change before a checked assignment/input/increment commits. */
async function verifySubrangeWrites(compile) {
    for (const [source,input,expected] of [
        ['program p; var value:1..3; begin value:=2; readln(value) end.','4\n',2],
        ["program p; var value:'a'..'c'; begin value:='b'; readln(value) end.",'z\n',98],
        ['program p; var value:1..3; begin value:=3; inc(value) end.','',3],
        ['program p; var value:1..3; begin value:=2; value:=4 end.','',2]
    ]) {
        const result = compile(source);
        assert(!result.diagnostic, JSON.stringify(result.diagnostic));
        const host = createPas2WasmRuntime(input, () => {});
        const {instance} = await WebAssembly.instantiate(result.binary, {pas2wasm_v1:host.imports});
        host.attach(instance);
        assert.throws(() => instance.exports.run(), error => error.code === 'R_RANGE');
        const memory = new DataView(instance.exports.memory.buffer);
        assert.equal(memory.getInt32(262144, true), expected, 'Failed narrowing changed its payload');
        assert.equal(memory.getUint8(262152), 1, 'Failed narrowing changed initialization');
    }
}

/* Run a development tool with bounded runtime and useful failure output. */
function tool(command, args, options = {}) {
    const result = spawnSync(command, args, {encoding: 'utf8', timeout: 15000, ...options});
    if (result.error) throw result.error;
    assert.equal(result.status, 0, `${command} ${args.join(' ')}\n${result.stdout}\n${result.stderr}`);
    return result;
}

/* Observe the ABI's atomic failure contract after a trap, before discarding memory. */
function verifyStringWrites() {
    const memory = new WebAssembly.Memory({initial:1, maximum:1024});
    const host = createPas2WasmRuntime('é\n€\n', () => {});
    host.attach({exports:{memory}});
    const bytes = new Uint8Array(memory.buffer);
    host.imports.string_read(0, 2, 1, 1);
    const original = bytes.slice(0, 32);
    assert.throws(() => host.imports.string_read(0, 2, 8, 3),
        error => error.code === 'R_STRING_CAPACITY' && error.line === 8 && error.column === 3);
    assert.deepEqual(bytes.slice(0, 32), original, 'Failed input must not partially change storage');
    assert.throws(() => host.imports.string_assign(0, 1, 0, 9, 2),
        error => error.code === 'R_STRING_CAPACITY');
    assert.deepEqual(bytes.slice(0, 32), original, 'Failed assignment must not change aliased storage');
    assert.throws(() => host.imports.string_concat(0, 2, 0, 0, 10, 2),
        error => error.code === 'R_STRING_CAPACITY');
    assert.deepEqual(bytes.slice(0, 32), original, 'Failed concatenation must not change storage');
    assert.throws(() => host.imports.string_assign(0, 2, -1, 1, 1),
        error => error.code === 'R_MEMORY');
    assert.deepEqual(bytes.slice(0, 32), original);
    const descriptor = new DataView(memory.buffer, 64, 16), target = 64 | 0x80000000;
    descriptor.setUint32(0, 0, true); descriptor.setUint32(4, 1, true);
    assert.throws(() => host.imports.char_store(target, 0x1f600, 1, 1),
        error => error.code === 'R_STRING_CAPACITY');
    assert.throws(() => host.imports.char_store(target, 0xd800, 1, 1),
        error => error.code === 'R_CHARACTER');
    assert.deepEqual(bytes.slice(0, 32), original, 'Rejected character writes must be atomic');
    const changed = createPas2WasmRuntime('abc\nx\n', () => {});
    changed.attach({exports:{memory}});
    changed.imports.string_read(0, 8, 1, 1);
    descriptor.setUint32(4, 3, true);
    assert.equal(changed.imports.char_load(target, 1, 1), 99);
    changed.imports.string_read(0, 8, 1, 1);
    const shortened = bytes.slice(0, 32);
    assert.throws(() => changed.imports.char_load(target, 1, 1), error => error.code === 'R_BOUNDS');
    assert.throws(() => changed.imports.char_store(target, 97, 1, 1), error => error.code === 'R_BOUNDS');
    assert.deepEqual(bytes.slice(0, 32), shortened, 'Invalidated views must not overwrite storage');
}

/* Verify native/Wasm/WAT parity and optional FPC comparison, using temporary files. */
async function main() {
    verifyStringWrites();
    await require('./io.js')();
    const compile = await createCompiler(require('../build/pas2wasm.js'));
    await verifySubrangeWrites(compile);
    const version = fs.readFileSync(path.join(__dirname, '../VERSION'), 'utf8').trim();
    assert.equal(compile.version, version, 'Wasm compiler embeds VERSION');
    assert.equal(tool(nativeExecutable, ['--version']).stdout, `Pas2Wasm ${version}\n`);
    const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'pas2wasm-test-'));
    let roundtrips = 0, comparisons = 0;
    try {
        await require('./heap.js')(compile, tool, directory);
        const legacy = path.join(directory, 'legacy-io.wasm');
        tool('wasm-as', [path.join(__dirname, 'fixtures/legacy-io.wat'), '-o', legacy]);
        assert.deepEqual(await execute(fs.readFileSync(legacy), '12 ignored\n1.25 junk\nérest\nhello\nskip\n7\n'),
            {output:'12\n1.25\né\nhello\n7\n'}, 'Pre-M11 module line-import compatibility');
        for (const test of cases) {
            const input = path.join(directory, `${test.name}.pas`);
            const output = path.join(directory, `${test.name}.wasm`);
            fs.writeFileSync(input, test.source);
            const result = compile(test.source, true);
            const native = spawnSync(nativeExecutable, ['--diagnostics-json', '--emit-wat', '-o', output, input], {encoding:'utf8'});
            if (test.error) {
                assert.equal(result.diagnostic?.code, test.error, test.name);
                assert.equal(native.status, 1, test.name);
                assert.deepEqual(JSON.parse(native.stderr), result.diagnostic, test.name);
                assert(!fs.existsSync(output), test.name);
                continue;
            }
            assert(!result.diagnostic, `${test.name}: ${JSON.stringify(result.diagnostic)}`);
            assert.equal(native.status, 0, `${test.name}: ${native.stderr}`);
            assert(WebAssembly.validate(result.binary), test.name);
            assert.deepEqual(result.binary, new Uint8Array(fs.readFileSync(output)), test.name);
            const watFile = output.replace(/\.wasm$/, '.wat');
            assert.equal(result.wat, fs.readFileSync(watFile, 'utf8'), test.name);
            const plain = compile(test.source);
            assert.equal(plain.wat, null, test.name);
            assert.deepEqual(plain.binary, result.binary, test.name);
            const actual = await execute(result.binary, test.input, test.randomSeed);
            if (test.runtime) {
                assert.equal(actual.code, test.runtime, test.name);
                if (test.runtimeLine !== undefined) assert.equal(actual.line, test.runtimeLine, test.name);
                if (test.runtimeColumn !== undefined) assert.equal(actual.column, test.runtimeColumn, test.name);
            } else assert.deepEqual(actual, {output:test.output}, test.name);
            const assembled = output.replace('.wasm', '.roundtrip.wasm');
            tool('wasm-as', [watFile, '-o', assembled]);
            const roundtrip = fs.readFileSync(assembled);
            assert(WebAssembly.validate(roundtrip));
            assert.deepEqual(await execute(roundtrip, test.input, test.randomSeed), actual, test.name);
            roundtrips++;
            if (test.portable) {
                const executable = path.join(directory, test.name);
                tool('fpc', ['-Mobjfpc', '-Co', `-o${executable}`, input]);
                const fpc = tool(executable, [], {input:test.input || ''});
                assert.equal(fpc.stdout, test.output, `Free Pascal: ${test.name}`);
                comparisons++;
            }
            if (!test.runtime) assert.deepEqual(await execute(result.binary, test.input, test.randomSeed), actual, 'fresh repeated run');
        }
        assert.equal(compile('program p; begin end.', false, 'x86_64').diagnostic.code, 'E_TARGET');
        assert.equal(compile(' '.repeat(262145)).diagnostic.code, 'E_LIMIT');
        assert.equal(compile('program p; begin writeln(' + '('.repeat(200) + '1' + ')'.repeat(200) + ') end.').diagnostic.code, 'E_LIMIT');
        const targetSource = path.join(directory,'target.pas');
        fs.writeFileSync(targetSource, 'program p; begin end.');
        const defaultOutput = path.join(directory,'default.wasm');
        const explicitOutput = path.join(directory,'explicit.wasm');
        tool(nativeExecutable,['-o',defaultOutput,targetSource]);
        tool(nativeExecutable,['--target=wasm32','-o',explicitOutput,targetSource]);
        assert.deepEqual(fs.readFileSync(defaultOutput), fs.readFileSync(explicitOutput));
        assert(!fs.existsSync(defaultOutput.replace('.wasm','.wat')));
        const largeSource = path.join(directory,'large.pas');
        fs.writeFileSync(largeSource,' '.repeat(262145));
        const large = spawnSync(nativeExecutable,['--diagnostics-json','-o',path.join(directory,'large.wasm'),largeSource],{encoding:'utf8'});
        assert.equal(JSON.parse(large.stderr).code,'E_LIMIT');
        const unsupported = spawnSync(nativeExecutable,['--diagnostics-json','--target=x86_64','-o',path.join(directory,'unsupported.wasm'),targetSource],{encoding:'utf8'});
        assert.equal(JSON.parse(unsupported.stderr).code,'E_TARGET');
        assert(!fs.existsSync(path.join(directory,'unsupported.wasm')));
        const invalidUtf8 = path.join(directory,'encoding.pas');
        fs.writeFileSync(invalidUtf8, Buffer.from([0xc0,0xaf]));
        const invalid = spawnSync(nativeExecutable,['--diagnostics-json','-o',path.join(directory,'bad.wasm'),invalidUtf8],{encoding:'utf8'});
        assert.equal(JSON.parse(invalid.stderr).code,'E_ENCODING');
        const runtime = createPas2WasmRuntime('', () => {});
        assert.throws(() => runtime.imports.write_utf8(-1, 2), {code:'R_MEMORY'});
        assert.throws(() => createPas2WasmRuntime('a'.repeat(1048577), () => {}), {code:'R_INPUT_LIMIT'});
        const memoryHost = createPas2WasmRuntime('', () => {});
        memoryHost.attach({exports:{memory:new WebAssembly.Memory({initial:1})}});
        assert.throws(()=>memoryHost.imports.write_utf8(65536,1),{code:'R_MEMORY'});
        console.log(`${cases.length} acceptance cases passed; ${roundtrips} WAT roundtrips; ${comparisons} Free Pascal comparisons.`);
    } finally { fs.rmSync(directory, {recursive:true, force:true}); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });
