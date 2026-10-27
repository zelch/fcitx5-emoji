# fcitx5-emoji

A native Fcitx5 emoji picker with Lua word-prefix search and personal variant
preferences. Press **Ctrl+Alt+E**, type `cat smil`, choose an emoji, and press
Enter. Space separates query words. Escape cancels without inserting the query.

The picker uses Fcitx's normal candidate panel and follows the application
caret. It reuses the temporary-mode, input-buffer, compose-key, and candidate-list
components used by Quick Phrase. On older Fcitx versions, event watchers and
per-context properties provide the temporary mode. It runs entirely inside Fcitx; no service,
network request, language model, or D-Bus round trip is needed for searches.

## Build and install

Requires Fcitx5 **5.1.3 or newer** with development headers, Lua **5.3 or newer**,
zlib **1.2.9 or newer**, ICU (uc and i18n), CMake, and a C++20 compiler. Fcitx's
emoji dictionaries must be installed, usually by the distribution's Fcitx data
package. `fcitx5-lua` is not required: the native addon owns its Lua state.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
    -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

For an installation in your home directory, use:

```sh
cmake -S . -B build-local -DEMOJI_USER_INSTALL=ON \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
cmake --build build-local
cmake --install build-local
```

This defaults to `~/.local`, writes the absolute library path into the addon
descriptor, and requires neither sudo nor `FCITX_ADDON_DIRS`. System Fcitx,
Lua, ICU, and emoji dictionaries are still used. Restart Fcitx to load it.
If you set `XDG_DATA_HOME`, also pass `-DCMAKE_INSTALL_DATADIR="$XDG_DATA_HOME"`
so Fcitx can discover the descriptor and Lua files there. A custom install prefix
needs its data directory in Fcitx's search paths; the default `~/.local/share`
is discovered automatically. The generated descriptor belongs to this user's
installation and should be regenerated if the library is moved.

For a system-wide install, use the prefix containing your Fcitx installation;
`/usr` is a common example.
For a Fcitx built in a custom prefix, also pass `-DCMAKE_PREFIX_PATH=/path/to/prefix`.
The addon installs under `lib/fcitx5` (or the platform's configured library
directory), with its descriptor and Lua files under `share/fcitx5`.
Restart Fcitx after installation. In **Configure Fcitx → Addons → Emoji Picker**,
configure activation shortcuts and variant preferences. An empty shortcut list
disables activation. Avoid assigning a shortcut already consumed by another IM
or addon. Shortcuts are modifier chords, not successive multi-key sequences.
Variant dropdowns include emoji examples. Person examples use the saved gender,
tone, and hair preferences; apply changes and reopen the menu to refresh examples
in the other dropdowns. The standard Fcitx configuration editor cannot update
these labels live while editing. Rendering depends on your configuration UI's
emoji fonts, especially for newer direction sequences.

Verified against Fcitx **5.1.3**, **5.1.19**, and **5.1.23**, including the
native picker integration tests. On releases without candidate comments, names
appear beside the emoji in the candidate text; selection still inserts only the
emoji. Build against the Fcitx version installed on your target system.

## Interaction

| Key | Action |
| --- | --- |
| Configured activation shortcut | Open the picker, or cancel an open picker |
| Text, including Space | Edit the search query |
| Up / Down, Shift+Tab / Tab | Select previous / next candidate |
| Page Up / Page Down | Change candidate page |
| Left / Right, Home / End | Move the query cursor |
| Backspace / Delete | Edit the query at the cursor |
| Ctrl+U | Clear the query |
| Enter | Insert the selected emoji |
| Escape | Cancel |

Clicking a candidate inserts it. An empty query browses the catalogue. The
query is limited to 256 Unicode characters. Focus loss, input reset, or a change
of input method cancels the picker. Activation leaves existing compositions
alone and is ignored in password or sensitive fields.

## Matching and preferences

Each distinct query word must match the beginning of a word in the emoji's
annotations or its canonical Unicode name. Words can match different
annotation strings, and their order does not matter: `cat smil` and `smil cat`
return the same results. Exact word matches rank before incomplete prefixes.
ICU supplies NFKC case folding and Unicode word boundaries, including scripts
that do not use spaces. Typo correction is not performed.

The native reader uses installed `emoji/data/*.dict` files directly, without
the upstream emoji module's multiword filtering or two-result limit. Regional
locale data falls back per emoji to its parent locales and then English. For
example, `en_GB` also supplies entries from `en`; `zh_TW` maps to `zh_Hant`.
English Unicode names supplement annotations because the dictionaries omit
separate names and country flags frequently have only the annotation `flag`.
Display labels are those canonical English names; localized annotations remain
searchable.

All appearance settings are **ranking preferences**, never filters:

- Person gender: no preference, neutral/person, woman, or man. For couples this
  is the first person's preference.
- Second person's gender: independently selects the second person in couples.
- Skin tone: no preference, unmodified, or one of the five Unicode modifiers.
- Second person's skin tone: independent, or follows the first preference.
- Hair: no preference, red, curly, white, or bald.
- Facing direction: no preference, left, or right where supported by metadata.
- Presentation: no preference, emoji style, or text style for eligible symbols.
- Country or subdivision flag: no preference or a supported flag, chosen by name.

Preference ordering first swaps equally relevant members of the same variant
family in their existing result slots, then moves variants conflicting with a
configured preference to the bottom of the full results. For example, a selected
skin tone shows the preferred waving hand and raised hand before the other
waving-hand tones. With no preferences, source order breaks relevance ties.
Explicit searches still determine eligibility and relevance:
`man raising hand` remains useful when the default is woman. Matching explicit
gender, tone, hair, and direction words also suppresses the corresponding
preference. No alternative is removed; all matches remain available by paging.
Family household compositions are separate meanings and do not inherit gender
preferences. Unsupported combinations are never synthesized.

## Data and scope

`tools/generate_metadata.py` generates `data/metadata.lua` and the flag dropdown
from pinned Unicode **Emoji 14.0** data, matching the dictionaries in the sibling
Fcitx checkout. It records family relationships, variant attributes, names, and
valid text-presentation sequences. Builds and runtime use the committed output
and require no downloads. Regenerate it with:

```sh
python3 tools/generate_metadata.py
python3 tools/generate_metadata.py --check
```

Emoji 14.0 has no right-facing person variants, so the direction setting has no
effect on this catalogue. Updating to newer dictionary and metadata versions is
needed to make those variants available. Unknown emoji in a newer installed
dictionary remain searchable by annotation, but have no inferred variant
preferences until metadata is updated. Unicode font support determines how an
application renders valid emoji and text sequences.

## Architecture

- `src/picker.cpp`: Fcitx integration, editing, navigation, selection, lifecycle.
- `src/config.h`: standard Fcitx GUI configuration and multiple key triggers.
- `src/engine.cpp`: Lua ownership, ICU word splitting, locale lookup, bounded
  dictionary decompression through zlib. Dictionaries are validated before use.
- `lua/search.lua`: dictionary decoding, cached word index, matching, ranking.
- `tools/generate_metadata.py`: reproducible metadata generation.

Lua calls are direct and synchronous. A dictionary is indexed on first use of a
language and cached thereafter. Configuration changes refresh open pickers;
Fcitx's configuration reload also recreates the Lua state and reloads its files.
Missing or corrupt data appears as a picker status message; Escape still works.

## Tests

Tests additionally require Python 3 and Fcitx's development test frontend and
test IM modules (build recent Fcitx with `ENABLE_TESTING_ADDONS=ON`;
5.1.3 builds these modules by default).

```sh
cmake -S . -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

`EMOJI_TEST_DICTIONARY_DIR` can point to a source-tree dictionary directory when
it differs from the installed location. The native integration test launches an
isolated Fcitx instance, loads the real addon, and sends real key events. It
checks both shortcuts, multiword editing, variant ranking, paging, commitment,
cancellation, candidate clicks, preedit clicks, focus loss, input reset, input
method changes, sensitive fields, existing composition, saved settings,
live settings changes, reload, and destruction while open. It does not use the
user's Fcitx configuration or restart their desktop session.

Engine tests cover matching, source order, variant preferences, Unicode,
regional/language fallback, and invalid input. Metadata tests check regeneration
and family relationships. A desktop rendering check is useful for your selected
Fcitx UI and fonts; the automated tests verify framework behavior headlessly.

Project code is licensed under **LGPL-2.1-or-later**; see `LICENSE.md`.
The compatibility layer includes event handling adapted from Fcitx5,
Copyright (c) 2026 CSSlayer <wengxt@gmail.com>, under the same license.
Bundled and generated Unicode data is licensed separately under the Unicode
license in `data/LICENSE-UNICODE`; upstream data notices are preserved.
See `LICENSE.md` for copyright statements and the scope of each license.
