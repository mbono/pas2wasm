# Copyright (c) 2026 Mariano Bono
# SPDX-License-Identifier: MIT

"""Check maintained Markdown's local links/heading anchors without build tools."""
import re
from pathlib import Path
from urllib.parse import unquote


def main():
    """Resolve local references relative to each owning document, including fragments."""
    files = [Path('AGENTS.md'), Path('README.md'), *sorted(Path('docs').glob('*.md'))]
    errors = []
    links = 0
    for file in files:
        for match in re.finditer(r'\[[^\]]*\]\(([^\s)]+)\)', file.read_text()):
            target = match[1]
            if re.match(r'[a-z]+:', target):
                continue
            path, _, anchor = unquote(target).partition('#')
            resolved = (file.parent / path).resolve() if path else file.resolve()
            links += 1
            if not resolved.exists():
                errors.append(f'{file}: missing {target}')
                continue
            if anchor and resolved.suffix == '.md':
                headings = [re.sub(r'[^\w\-\s]', '', h.lower()).replace(' ', '-')
                            for h in re.findall(r'^#{1,6}\s+(.+)$', resolved.read_text(), re.M)]
                if anchor not in headings:
                    errors.append(f'{file}: missing {target}')
    if errors:
        raise SystemExit('\n'.join(errors))
    print(f'{len(files)} Markdown files: {links} local links/anchors checked.')


if __name__ == '__main__':
    main()
