# Portuguese (Brazil)

An original, unofficial translation made for this PC port from the game's official English text. No fan translation
was used. Portuguese (Brazil) is a Menu & Subtitles language of every supported build and needs no extra package or
other copy of the game. With it selected the launcher, the port's in-game screens and the Trophies page are in
Portuguese too.

The game runs in a native slot; when it reads its archives the port serves translated copies of that slot's text
(`launcher/game_translation.h`, `runtime_file_overlay` in `src/runtime_file.c`). The archives on disk are never
written. Each translation is bound to the SHA-256 of the English it was made from: a record whose installed English
differs keeps the game's text, and the run log counts why. Accents are never removed: a record with a letter the
build's fonts cannot draw keeps the slot's text and the log names the letter.

The six chapter prologue pages are glyph layouts (`Installation.mbs`, `GUI/REHD_Installation.mbs`) over one atlas, not
text: on Europe, where Portuguese (Brazil) runs over the Spanish slot, `visual/prologues-ptbr.oslv` serves both pages
and their atlas respelled from the canonical prologue map (`launcher/prologue_translation_table.inc`, 41 records). A
page and its atlas are laid on together or not at all; any other build or language keeps them as shipped.

| Build | Slot | Records in Portuguese | Letters added to the game's fonts |
| --- | --- | --- | --- |
| CUSA05083 1.00 USA | English | 14,925 / 14,925 | 31 (`ã õ Ã Õ Ç`, and every accent in HD_font_c) |
| CUSA05262 1.00, 1.01 Europe | Spanish | 14,925 / 14,925 | 10 (`ã õ Â Ã Õ`) |
| CUSA01290 1.02 Japan | Japanese | 14,148 / 14,925 | 52, and 26 Shift-JIS codes for the story files |
| CUSA01290 1.00 Japan | Japanese | 9,321 / 14,925 | 52, and 26 Shift-JIS codes |
| CUSA05083 1.01 USA, CUSA03873 HK and Asia | English, Chinese | not measured: no local files; accented records keep the slot's text until `font/glyphs.json` knows the executable | - |

## Fonts

The game draws text from its own atlases (`HD_font_a/b/c.ftx` in the BT/BOOT archive). Each executable holds static
font descriptors: a 16-byte header (texture size and count, cell size, columns, rows) and 256 code page pointers to
8-byte glyph records (atlas cell, width, offset, metrics). The European fonts hold every Portuguese letter except
`ã õ Â Ã Õ`; the USA's lack `ã õ Ã Õ Ç` (HD_font_c every accent); the Japanese fonts have none.
`scripts/localization/glyphs.py` finds the descriptors, groups those sharing an atlas, picks free cells whose blocks
hold no other glyph and writes `font/glyphs.json` (fingerprints and cell numbers only). For Portuguese (Brazil) only,
`launcher/latin_font.cpp` draws each missing letter into the player's own atlas from the font's own glyphs (the mark of
`ñ`, `Ñ`, `Ê` or `ç`, or the font's punctuation where it has no accents), served through the overlay;
`src/text_glyphs.c` adds the letters' records to the loaded descriptors and gives unassigned Shift-JIS codes the
letters in the Japanese executable's decoder table. An atlas whose fingerprint differs is left alone and logged.

## Contents

| Path | Contents |
| --- | --- |
| `sheets/msgsheet.json`, `story.json`, `archive.json`, `credits.json` | One entry per distinct English text: `source_sha256`, `pt_br`, `at` (its first record) |
| `font/glyphs.json` | Per executable: font descriptors, atlas fingerprints, free cells and how each letter is drawn |
| `visual/title-exit-ptbr.dcpx` | SAIR DO JOGO on the title menu, against the English title layer; laid on whole or not at all |
| `visual/prologues-ptbr.oslv` | The chapter prologue pages and their glyph atlas for the Spanish slot of Europe (`scripts/localization/installation_poems.py`) |
| `visual/interface-ptbr.oslv` | Interface layouts (HUD, save/load, YES/NO, Bookshelf status, stage cards, menu tabs) respelled from the shared glyph font; `visual/labels.tsv` is its English -> Portuguese table |
| `visual/official-resources.tsv`, `visual/coverage.tsv` | Every resource the European game localizes (from the Spanish slot) and its Portuguese status |
| `visual/INVENTORY.md` | Official localization parity by group, and what is blocked |
| `trophies.tsv` | The 48 trophies, bound to their English by SHA-256 |
| `glossary.tsv` | Terminology: `category`, `source`, `pt_br`, `status`, `notes` |

The English is in `localization/en`. Credits translate only role headings; names stay as they are.

## Not translated

- Baked art the official languages localize and that is not generated yet (`visual/INVENTORY.md`, 61 of 757 resources
  blocked, each with its reason): the Bookshelf book frames, the 44 manual pages, the Skill screen and the Classic logo
  layout. The title rows, stage and location cards, chapter and act titles, prologue pages and interface words are done.
- Japanese records whose numbers, neighbours or arguments do not prove them to be the English record (777 on 1.02),
  Japanese credits (laid out differently) and 12 ArchiveViewer pages with no English counterpart block.

## Workflow

Run from the repository root; working files go to `cache/localization/pt-BR/`, which git ignores.

```
python scripts/localization/ptbr.py batches --wave rest   # context batches of what is untranslated
python scripts/localization/ptbr.py check work.tsv --limits
python scripts/localization/ptbr.py merge work.tsv --limits
python scripts/localization/ptbr.py validate --limits     # must end with 0 errors
python scripts/localization/review.py                     # suspicious entries, UI width budgets
python scripts/localization/ptbr.py parity                # every build
python scripts/localization/glyphs.py generate            # font/glyphs.json from the prepared games (numpy, Pillow)
python scripts/localization/glyphs.py check               # every corpus letter has a glyph on every build listed
python scripts/localization/make_visuals.py               # visual/interface-ptbr.oslv from visual/labels.tsv (numpy, Pillow)
python scripts/localization/installation_poems.py write  # visual/prologues-ptbr.oslv from the prologue map (numpy, Pillow)
python scripts/localization/visual_parity.py check        # every officially localized resource has a status
```

## Rules

- Translate the English; other languages are context only.
- Keep every control sequence (`@#(N)`, `##(KEY)`, `$$(N)`, `@c(..)…@/c`, `@s`, `@y`, `@w(N)`, `@b`, `@i`, `@C`, `@F`)
  and the printf conversions of a format record in order. `20% or` is prose.
- Never use more lines than any shipped language uses for the record; ArchiveViewer pages keep CRLF.
- Letters: ASCII and `á à â ã é ê í ó ô õ ú ü ç` with capitals (no `ª º`: no font draws them).
