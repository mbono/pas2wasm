# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Verify the complete pinned tutorial corpus before browser execution."""
import hashlib
import json
from pathlib import Path


def verify(cases):
    """Return unchanged original cases; their expected I/O lives in cases.json."""
    manifest = json.loads(Path(__file__).with_suffix('.json').read_text())
    assert len(manifest['examples']) == 23
    assert len({item['file'] for item in manifest['examples']}) == 23
    result = []
    for item in manifest['examples']:
        matches = [case for case in cases if case['name'] == item['case']]
        assert len(matches) == 1, item['file']
        case = matches[0]
        source = case['source'].encode('utf-8')
        blob = hashlib.sha1(b'blob '+str(len(source)).encode()+b'\0'+source).hexdigest()
        assert blob == item['blob'], f'Original tutorial source changed: {item["file"]}'
        assert isinstance(case.get('output'), str) and not case.get('error') and not case.get('runtime'), item['file']
        result.append(case)
    return result
