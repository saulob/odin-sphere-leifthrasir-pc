# Controller prompts: how `launcher/prompts/xbox-prompts.dcpx` is made

The game draws its button prompts from two places:

- the button atlas, the 512 × 256 BC3 texture of `Interface.ftx` (Classic, in `Odin2_OR*.CPK`) and
  `GUI/HD_Interface.ftx` (Refined, in `Odin2_RE*.CPK`): discs, symbols, shoulder keys and the START/SELECT/OPTIONS/
  SHARE/TOUCHPAD labels that the sprite definitions (`Interface.mbs`, `BUTTON00`…`BUTTON58`) cut out of it;
- a second button set in `OnMemory.ftx` / `OnMemory/HD_OnMemory.ftx` (`BUTTON_A`…`BUTTON_DW`): L1/L2/R1/R2 keys and
  symbol discs in `HD_OnMemory_arranged020`, a SELECT tag in `arranged000` and a START tag in `arranged010`.

Painting Xbox glyphs into the same rectangles changes the prompts without touching anything else. The launcher applies
the result as a block patch to the installed archives (`launcher/cpk_patch.h`, `launcher/controller_prompts.h`), the
same way Dragon's Crown Pro PC does.

The button atlas holds the same parts as Dragon's Crown Pro's `Interface_arranged000` (1024 × 512 BC7) at exactly half
the size, laid out differently. So its Xbox parts are Dragon's Crown Pro PC's own: `draw_osl.py interface` applies that
port's `xbox-prompts.dcpx` to its `stage/Interface_US.ftx`, reduces each changed part 2:1 and places it in Odin's
rectangle, registered against the original artwork (every part matches within 12 RMSE). The OnMemory glyphs have no
Dragon's Crown counterpart; they are painted the way its `draw_dcp.py` paints, with the same Xbox colours and Poppins
Bold (SIL OFL, fonts.google.com/specimen/Poppins).

Every build and language ships its own copy of these files (the atlas pixels are the same in the USA and European
copies; the Japanese copy is encoded differently). The patch has one target per copy, named as the archive names it and
keyed by its CRC32, so one patch serves all supported builds and an unknown copy is left alone.

These scripts are development tools (Python 3.11+, `numpy`, `scipy`, `Pillow`); players never run them, and the patch
is committed.

| Script | What it does |
| --- | --- |
| `cpk.py` | CRI CPK reader: `list`, `header`, `extract`, `extract-matching`, `selftest` (from Dragon's Crown Pro PC). |
| `ftex.py` | Vanillaware FTEX packs: `list` the textures, `dump` them as PNG (from Dragon's Crown Pro PC). |
| `draw_osl.py` | Paints one atlas: `interface` from Dragon's Crown parts, `onmemory000/010/020` with Poppins Bold. Writes the atlas and a mask of the changed pixels. |
| `bc3.py` | A BC3 encoder (numpy). |
| `patch_ftx.py` | Re-encodes only the changed 4 × 4 blocks and splices them into the pack. |
| `make_dcpx.py` | Diffs original and patched files into the `.dcpx` block patch the launcher embeds. |
| `make_prompts.py` | Builds the whole patch from the prepared game folders: every copy of the four files in every archive. |

## Rebuilding the patch

With every supported build installed (`game\prepared\<build>`), Dragon's Crown Pro 1.03's unpatched
`stage/Interface_US.ftx` (from its `DRACRO_US.CPK`) and that port's `launcher/prompts/xbox-prompts.dcpx`:

```
python scripts/prompts/cpk.py extract <Dragon's Crown 1.03>/DRACRO_US.CPK stage/Interface_US.ftx work
python scripts/prompts/make_prompts.py game/prepared launcher/prompts/xbox-prompts.dcpx \
    --dc-pack work/Interface_US.ftx --dc-patch <dragons-crown-pro-pc>/launcher/prompts/xbox-prompts.dcpx \
    --font Poppins-Bold.ttf
```

To look at one atlas first, `ftex.py dump` the pack and run `draw_osl.py` on the PNG.

## Formats, briefly

- **CPK**, **FTEX**, **DCPX** and the **undo record**: as in Dragon's Crown Pro PC's `scripts/prompts/README.md`. The
  interface textures here are BC3 (16-byte blocks, like BC7), tile mode 13 (Thin_1dThin), no mipmaps. A DCPX file may
  name an entry more than once, one target per copy of the file.
