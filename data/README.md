The pinned Unicode sources are Emoji 18.0 and UCD 18.0:

- https://github.com/unicode-org/unicodetools/blob/main/unicodetools/data/emoji/18.0/emoji-test.txt
- https://github.com/unicode-org/unicodetools/blob/main/unicodetools/data/ucd/18.0.0/emoji/emoji-variation-sequences.txt

The files retain their upstream notices. LICENSE-UNICODE is Unicode's current
license (Unicode License V3, SPDX: `Unicode-3.0`) from
https://github.com/unicode-org/unicodetools/blob/main/LICENSE.
The pinned sources retain their Copyright (c) 2026 Unicode, Inc. notices;
the license text carries Copyright (c) 2001–2024 Unicode, Inc.
The same separate Unicode data license applies to the generated
`data/metadata.lua` and `src/countries.inc`, whose generated headers record
their provenance and license. The project's LGPL-2.1-or-later code license
does not replace these Unicode data terms.
Metadata and the country dropdown are reproducibly generated from those sources
by ../tools/generate_metadata.py. Generated text presentation sequences are
limited to those listed in Unicode's variation-sequence data. Variant families
use canonical Unicode names and sequence attributes, with explicit mappings for
legacy compact sequences. Household compositions remain separate families.
The installed Fcitx dictionaries supply annotations; they are not copied here.

The pinned Emoji 18.0 catalogue contains 3,963 fully-qualified sequences.
Emoji absent from installed dictionaries remain searchable by Unicode name.
