/*
 * Copyright (c) 2026 Mariano Bono
 * SPDX-License-Identifier: MIT
 */
const assert = require('node:assert/strict');
const {createHash} = require('node:crypto');
const manifest = require('./tutorials.json');

/* Pin the full original corpus while keeping source/input/output in shared cases. */
module.exports = function verifyTutorials(cases) {
    assert.equal(manifest.examples.length, 23);
    assert.equal(new Set(manifest.examples.map(item => item.file)).size, 23);
    return manifest.examples.map(item => {
        const matches = cases.filter(test => test.name === item.case);
        assert.equal(matches.length, 1, item.file);
        const test = matches[0], bytes = Buffer.from(test.source, 'utf8');
        const blob = createHash('sha1').update(`blob ${bytes.length}\0`).update(bytes).digest('hex');
        assert.equal(blob, item.blob, `Original tutorial source changed: ${item.file}`);
        assert.equal(typeof test.output, 'string', item.file);
        assert(!test.error && !test.runtime, `Tutorial must compile and run: ${item.file}`);
        return test;
    });
};
