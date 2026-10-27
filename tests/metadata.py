#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Validate meaningful family relationships, not just generated-file freshness."""
import importlib.util
import json
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('generator', root / 'tools/generate_metadata.py')
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)
source, _ = generator.generate()
rows = []
for line in source.splitlines():
    if line.startswith('{'):
        rows.append({k: json.loads(v) for k, v in re.findall(r'(\w+)=("(?:[^"\\]|\\.)*")', line)})
by_glyph = {row['emoji']: row for row in rows}
assert len(by_glyph) == len(rows), 'Duplicate generated sequences'
assert len([row for row in rows if not row['source']]) == 3963, 'Pinned Unicode catalogue changed'
def family(*glyphs):
    assert len({by_glyph[g]['family'] for g in glyphs}) == 1, glyphs
family('🙋', '🙋‍♀️', '🙋🏽‍♂️')
family('🧑‍⚕️', '👩‍⚕️', '👨🏻‍⚕️')
family('🧒', '👦', '👧')
family('🧓', '👴', '👵')
family('🫅', '🤴', '👸')
family('🎅', '🤶', '🧑‍🎄')
family('🧑', '👨', '👩‍🦰', '🧑‍🦲')
family('💑', '👩‍❤️‍👨', '👩🏻‍❤️‍👩🏿')
family('💏', '👨‍❤️‍💋‍👨', '👩‍❤️‍💋‍👩')
family('🤝', '🫱🏻‍🫲🏿')
assert by_glyph['🙋']['family'] != by_glyph['👋']['family']
assert by_glyph['👨‍👩‍👦']['family'] != by_glyph['👩‍👩‍👦']['family']
assert by_glyph['👨‍👩‍👦']['gender'] == ''
assert by_glyph['👩🏻‍❤️‍👩🏿']['tone'] == '1'
assert by_glyph['👩🏻‍❤️‍👩🏿']['tone2'] == '5'
assert by_glyph['💑']['gender2'] == 'Person'
assert by_glyph['🤼‍♀️']['gender2'] == 'Woman'
assert by_glyph['👯‍♂️']['gender2'] == 'Man'
assert by_glyph['🇨🇦']['country'] == 'CA'
assert by_glyph['🏴\U000e0067\U000e0062\U000e0073\U000e0063\U000e0074\U000e007f']['country'] == 'gbsct'
for row in rows:
    if row['source']:
        assert row['source'] in by_glyph and row['presentation'] == 'Text'
        assert row['family'] == by_glyph[row['source']]['family']
assert by_glyph['\U0001faeb']['name'] == 'cracking face'
family('\U0001faf9', '\U0001faf9\U0001f3fd')
assert by_glyph['\U0001faf9\U0001f3fd']['tone'] == '3'
family('🚶', '🚶‍➡️', '🚶🏽‍♀️‍➡️')
assert by_glyph['🚶']['direction'] == 'Left'
assert by_glyph['🚶‍➡️']['direction'] == 'Right'
# Direction attributes must not misclassify standalone arrows.
assert generator.attributes('➡️', 'right arrow', 'arrow')['direction'] == ''
right = generator.attributes('🚶‍➡️', 'person walking facing right', 'person-activity')
left = generator.attributes('🚶', 'person walking', 'person-activity')
assert right['direction'] == 'Right' and right['family'] == left['family']
print('Unicode family, attributes, coverage, and presentation checks passed')
