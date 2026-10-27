#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Elizabeth Loss-Cutler-Hull
# SPDX-License-Identifier: LGPL-2.1-or-later
"""Generate variant metadata from pinned Unicode files; no network at build time."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TONE_NAMES = ['light skin tone', 'medium-light skin tone', 'medium skin tone',
              'medium-dark skin tone', 'dark skin tone']
HAIR = {0x1F9B0: 'Red', 0x1F9B1: 'Curly', 0x1F9B2: 'Bald', 0x1F9B3: 'White'}
GENDER = {0x1F468: 'Man', 0x1F469: 'Woman', 0x1F9D1: 'Person',
          0x1F466: 'Man', 0x1F467: 'Woman', 0x1F9D2: 'Person',
          0x1F474: 'Man', 0x1F475: 'Woman', 0x1F9D3: 'Person',
          0x1F934: 'Man', 0x1F478: 'Woman', 0x1FAC5: 'Person',
          0x1F385: 'Man', 0x1F936: 'Woman'}

def attributes(glyph, name, subgroup):
    points = list(map(ord, glyph))
    tones = [str(p - 0x1F3FA) for p in points if 0x1F3FB <= p <= 0x1F3FF]
    genders = [GENDER[p] for p in points if p in GENDER]
    if 0x2640 in points:
        genders = ['Woman']
    elif 0x2642 in points:
        genders = ['Man']
    # Older compact sequences encode their people in a single character.
    compact = {0x1F46B: ['Woman', 'Man'], 0x1F46C: ['Man', 'Man'],
               0x1F46D: ['Woman', 'Woman'], 0x1F48F: ['Person', 'Person'],
               0x1F491: ['Person', 'Person']}
    if points[0] in compact:
        genders = compact[points[0]]
    if not genders and subgroup.startswith(('person', 'people-')):
        genders = ['Person']
    if ('wrestling' in name or 'with bunny ears' in name) and len(genders) == 1:
        # These legacy pair glyphs encode a shared gender for both people.
        genders = genders * 2
    # Family composition is not governed by the single/couple preferences.
    family_group = name.startswith('family')
    family = name
    for tone in sorted(TONE_NAMES, key=len, reverse=True):
        family = family.replace(tone, '')
    family = re.sub(r'\b(old man|old woman|older person)\b', 'older', family)
    family = re.sub(r'\b(boy|girl|child)\b', 'child', family)
    family = re.sub(r'\b(woman|man|person|women|men|people)\b', '', family)
    family = re.sub(r'\b(red|curly|white) hair\b|\bbald\b', '', family)
    family = re.sub(r'\bfacing (right|left)\b', '', family)
    family = re.sub(r'[,:\s]+', ' ', family).strip() or 'adult'
    if family in ('prince', 'princess', 'with crown'):
        family = 'with crown'
    if family in ('Santa Claus', 'Mrs. Claus', 'mx claus'):
        family = 'claus'
    if name.startswith('couple with heart'):
        family = 'couple with heart'
    elif name.startswith('kiss'):
        family = 'kiss'
    elif 'holding hands' in name:
        family = 'holding hands'
    elif name.startswith('handshake'):
        family = 'handshake'
    if family_group:
        family = re.sub(r'(?:,? (?:' + '|'.join(TONE_NAMES) + r'))', '', name)
    if name.startswith('flag:'):
        family = 'country flags' if len(points) == 2 and all(0x1F1E6 <= p <= 0x1F1FF for p in points) else 'subdivision flags'
    country = ''.join(chr(p - 0x1F1E6 + 65) for p in points) if family == 'country flags' else ''
    if family == 'subdivision flags' and points[0] == 0x1F3F4:
        country = ''.join(chr(p - 0xE0000) for p in points[1:] if 0xE0020 <= p < 0xE007F)
    return dict(family=family, gender='' if family_group else (genders[0] if genders else ''),
                gender2='' if family_group or len(genders) < 2 else genders[1],
                tone=tones[0] if tones else '0', tone2=tones[-1] if tones else '0',
                hair=next((HAIR[p] for p in points if p in HAIR), ''), country=country,
                direction='Right' if 'facing right' in name else ('Left' if 'facing left' in name else ''),
                presentation='Emoji')

def generate():
    rows = []
    subgroup = ''
    for line in (ROOT / 'data/emoji-test-14.0.txt').read_text().splitlines():
        if line.startswith('# subgroup:'):
            subgroup = line.split(':', 1)[1].strip()
        m = re.match(r'^([0-9A-F ]+); fully-qualified\s+# (\S+) E\S+ (.+)$', line)
        if m:
            glyph, name = m[2], m[3]
            rows.append(dict(emoji=glyph, name=name, **attributes(glyph, name, subgroup)))
    directional_families = {row['family'] for row in rows if row['direction'] == 'Right'}
    for row in rows:
        if row['family'] in directional_families and not row['direction']:
            row['direction'] = 'Left'
    presentations = set()
    for line in (ROOT / 'data/emoji-variation-sequences-14.0.txt').read_text().splitlines():
        m = re.match(r'^([0-9A-F]+) FE0E\s*;', line)
        if m:
            presentations.add(int(m[1], 16))
    for row in list(rows):
        base = row['emoji'].replace('\ufe0f', '')
        if len(base) == 1 and ord(base) in presentations:
            rows.append(dict(row, emoji=base + '\ufe0e', presentation='Text', source=row['emoji']))
    fields = ['emoji', 'name', 'family', 'gender', 'gender2', 'tone', 'tone2',
              'hair', 'country', 'direction', 'presentation', 'source']
    # Lua source is data only; JSON escaping is compatible for this Unicode text.
    out = ['-- Generated by tools/generate_metadata.py; Unicode Emoji 14.0.',
           '-- SPDX-FileCopyrightText: 2021 Unicode, Inc.',
           '-- SPDX-License-Identifier: Unicode-3.0',
           '-- Derived from Unicode Emoji/UCD 14.0; see data/README.md and data/LICENSE-UNICODE.',
           'return {']
    for row in rows:
        out.append('{' + ','.join(k + '=' + json.dumps(row.get(k, ''), ensure_ascii=False) for k in fields) + '},')
    out.append('}')
    countries = sorted({(r['country'], r['name'].removeprefix('flag: ')) for r in rows if r['country']})
    header = '// Generated by tools/generate_metadata.py\n'
    header += '// SPDX-FileCopyrightText: 2021 Unicode, Inc.\n'
    header += '// SPDX-License-Identifier: Unicode-3.0\n'
    header += '// Derived from Unicode Emoji 14.0; see data/README.md and data/LICENSE-UNICODE.\n'
    header += 'static const std::pair<const char *, const char *> countries[] = {\n'
    header += '\n'.join('{' + json.dumps(c) + ',' + json.dumps(n, ensure_ascii=False) + '},' for c, n in countries)
    header += '\n};\n'
    return '\n'.join(out) + '\n', header

if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    for name, content in zip(['data/metadata.lua', 'src/countries.inc'], generate()):
        path = ROOT / name
        if args.check:
            assert path.read_text() == content, f'{name} is stale'
        else:
            path.write_text(content)
