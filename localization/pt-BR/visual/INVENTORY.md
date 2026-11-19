# Official localization parity (visuals)

The Spanish archives of CUSA05262 1.01 hold exactly the entries the game localizes; every other entry comes from the
English ones. Each Spanish entry is therefore an officially localized resource, compared with the English entry of the
same path. Spanish is a map of where localized resources live and how their layouts change, never a translation
source: the Portuguese comes from the English.

- `official-resources.tsv`: the 757 entries (path, English and Spanish size and CRC32, format, classification), made by
  `python scripts/localization/visual_parity.py inventory`.
- `coverage.tsv`: the status of each, by pattern: `PT_BR_TRANSLATED`, `PT_BR_NOT_NEEDED`, `PT_BR_BLOCKED` (with the
  reason) or `DATA_UNAVAILABLE`.
- `python scripts/localization/visual_parity.py check` fails on a resource no pattern explains, and on a translated one
  no patch targets by its English size and CRC32. It also runs `prologue_map.py check`: the six chapter prologue
  containers' 41 records must be covered by the runtime translation map that swaps them before the text engine draws.

<!-- totals -->
| Group | Translated | Not needed | Blocked |
| --- | ---: | ---: | ---: |
| Bookshelf | 2 | 4 | 15 |
| HUD, save/load, confirmation | 8 | 0 | 0 |
| chapter prologue cards | 6 | 0 | 0 |
| ending | 0 | 11 | 0 |
| generic UI | 3 | 1 | 2 |
| manual/help | 0 | 1 | 43 |
| other | 0 | 10 | 0 |
| other menus | 6 | 7 | 0 |
| stage card that looks the same in English and Spanish | 0 | 1 | 0 |
| stage/location cards | 182 | 3 | 0 |
| text corpus | 436 | 0 | 0 |
| title | 12 | 3 | 1 |
| **Total** | **655** | **41** | **61** |
<!-- /totals -->

Duplicates (the Classic archive's copy of a Refined entry) take the status of the entry they copy.

## Portuguese (Brazil) visuals

| Patch | What |
| --- | --- |
| `interface-ptbr.oslv` | `GUI/REHD_Interface.mbs` and Classic `Interface.mbs` of USA 1.00 and Europe 1.00/1.01: 102 distinct labels (`labels.tsv`) respelled from the glyph font every language shares: HUD actions (FALAR, EXAMINAR, OUVIR, SAIR, ENTRAR, ALVO), save/load (DADOS, Tempo de jogo, Tempo total, Data, Dificuldade, SEM DADOS, DADOS DO SERVIDOR), SIM/NÃO, Bookshelf status (Nível, Ouro), difficulty names, stage cards (FASE DE ✕ BATALHA!, FASE DO SUBCHEFE!, ÁREA DE DESCANSO), menu tabs and the word sprites BOLSA, ITENS, EQUIPAMENTO, MAGIA, ATRIBUTOS and others. The Classic layout also gets its own labels (SUCESSO, Equipamento, Tempo de conclusão, Bônus de dano, Pontos transferidos, Dano, Última partida, CARREGANDO, LOJA, SALVAR, level-up banners, shop window, Arquivo de Textos), CARREGANDO on the book loading screen's circle (`arc_text.py`), the act titles of its L_* animations (`stage_acts.py`) and SEM DADOS without the English-only DATA channel drawn over it. The atlas is not touched. Made by `scripts/localization/make_visuals.py` |
| `title-exit-ptbr.dcpx` | The title menu over the English Exit Game layer: NOVO JOGO, CARREGAR, MODO CLÁSSICO (Refined), MODO REFINADO (Classic), SAIR DO JOGO, PRESSIONE QUALQUER BOTÃO (full size between the Spanish row's ornaments, painted into free atlas rows and drawn by the press keys' rewritten part and quad), the copyright and trademark lines, and the credit captions Apresentado por, Produtor and Diretor, set in texture 0 from the Spanish credit line of the same face (the capital A built from the face's own strokes; the personal-name rows untouched). Rows grow their atlas rectangle and layout quads as the Spanish 46-pixel MODO CLÁSICO row does. Made by `scripts/title_exit/make_title_exit.py --ptbr` (`title_rows.py`, `title_secondary.py`, `title_credits.py`) |
| `stage-cards-ptbr.oslv` | The 58 Refined stage and location title cards (`stage-cards.tsv`, wording from the corpus and glossary) of Europe 1.00/1.01, USA 1.00 and Japan 1.00/1.02, each card's blue, parchment and rotated renderings. Letters are cut from the player's own cards of all five European languages (one calligraphy style, scaled per card), accents composed from that style's own acute, circumflex, tilde (ES ñ) and cedilla marks, capitals U, J and é pinned by card fingerprint. Made by `scripts/localization/stage_cards.py`; every card is checked against its own decoded texture and no block outside the touched ones moves |

| `classic-cards-ptbr.oslv` | The 61 Classic cards (`NN.ftx` + `NN.mbs`) of every local build: a Portuguese letter atlas packed into the player's own card texture and a rebuilt layout of three layers (scattered, settled, stretched exit). Letters are the Western face of the Spanish, French, German and Italian cards, named by aligning each language's own location names to its sprites, then by shape; Q, Ú, ô, í and y pinned by card fingerprint; ã from the face's ñ tilde, ç from its comma. Made by `scripts/localization/classic_cards.py` |
| `stage-acts-ptbr.oslv` | The chapter and act titles of `GUI/REHD_StageName.mbs` (L_* and their _1 copies): VALQUÍRIA, O PRÍNCIPE POOKA, TERRA DAS FADAS, A ESPADA NEGRA, DESTINO, ARMAGEDOM, RODA DO DESTINO, Ato, Prelúdio, Epílogo, Cap. N. Lines an official language already spells are copied from it; the rest are set from the shared glyph font, glyphs relocated by sprite id in USA and Japan atlases. Made by `scripts/localization/stage_acts.py` |
| `mode-select-ptbr.oslv` | Mode Select: AÇÃO / RPG, Fácil, Normal, Difícil, Heroico from the shared illuminated glyph font. Made by `scripts/localization/family_respell.py mode_select` |
| `kitchen-ptbr.oslv`, `kitchen-layout-ptbr.oslv` | Kitchen: PARA LEVAR (Spanish slot), BÔNUS!/!!/!!! (circumflex drawn to the Spanish acute's size), Receita and Preço respelled from the shared script letters. Made by `sprite_slots.py kitchen` and `family_respell.py kitchen` |
| `interface-art-ptbr.oslv` | The painted sprites of the interface atlas (`GUI/HD_Interface.ftx`, Classic `Interface.ftx`; Europe 1.00 and 1.01 old and patched atlases, found by pixel search): HABILIDADE PRONTA and APTIDÃO PRONTA (condensed orange face), AÇÃO, CHEFE and FÚRIA (block face; tilde and cedilla drawn in its strokes), COMBO! (the French sprite with the space before the mark closed), painted into a donor language's slot. `interface-ptbr.oslv` also draws the official Spanish DESCANSAR plaque, NV tab and A icon, and the painted sprites, in the English layout (`layer_graft.py`). POW, EXP, DEF and HP stay as the corpus keeps them. Made by `scripts/localization/interface_art.py` (letters: `ink_font.py`) |
| `press-start-ptbr.oslv` | Classic PRESS ANY BUTTON (`op_press_start`): PRESSIONE QUALQUER BOTÃO from the Spanish and French rows of the title serif face, between the Spanish row's own ornaments, the tilde in the face's fill like its acute, at the official rows' size in the atlas's free rows; the layout takes the Italian layers with the row key pointed there. Made by `scripts/localization/press_start.py` |
| `worldmap-ptbr.oslv` | The world map banner (`GUI/HD_worldmap.ftx`, Classic `worldmap.ftx`, every local build): Map of ERION reads Mapa de ERION, set from the caption's own ink letters over its parchment; no official language localizes it. Made by `scripts/localization/map_title.py` |
| `bookshelf-new-ptbr.oslv` | Bookshelf NEW badge: NOVO from the Spanish NUEVO letters, painted into its slot. Made by `sprite_slots.py bookshelf_new` |
| `prologues-ptbr.oslv` | The chapter prologue pages of Gwendolyn, Cornelius, Mercedes, Oswald, Velvet and The End of Erion, and the Wheel of Fate title (`Installation.mbs`, `GUI/REHD_Installation.mbs` and their atlas `Installation.ftx` / `GUI/HD_Installation.ftx`, Spanish slot of Europe 1.00 and 1.01): every line respelled from the page's own glyphs in its three layers and four glow copies, screens keeping the records they show (centred when the Portuguese uses fewer lines). F, G and M (titles), M, O and ê come from the French, German and Italian atlases; ã and õ take the tilde of the Spanish ñ, ç the hooked tail of the face's comma, the hyphen the closing stroke of its z. Wording is the canonical prologue map (`launcher/prologue_translation_table.inc`). Laid on per archive with its atlas or not at all. Made by `scripts/localization/installation_poems.py` |

## Beyond the official localization

Some art every official language ships in English still reads in Portuguese (Brazil); no Spanish entry exists for it, so
these targets are the English entries of every local build:

- the world map banner (`worldmap-ptbr.oslv`);
- the press line of the single-row atlases: Classic `op_press_start` of USA and Japan, Refined
  `OnMemory/HD_op_press_start` of every build (`press-start-ptbr.oslv`);
- the Japanese interface layouts, whose labels are English (`interface-ptbr.oslv`, Classic and Refined);
- the verb of the executable's button prompts (Press, drücken, Appuyez sur, Premi, Pulsa): on Europe, where Portuguese
  (Brazil) runs over the Spanish slot, Pulsa reads Pressione in the loaded image (`src/text_glyphs.c`).

On a Spanish-slot run every visual delta is also laid on the English base archives' copies of its entries, so an entry
the Spanish archive lacks (the world map, OnMemory) or a read the game makes from the English layer is Portuguese too.

The six prologue pages are not drawn by the text engine: each line is a channel of the Installation layout whose
glyphs are parts of its atlas, so the page itself is the localized resource. The story containers `A0_2_1.txt` ...
`F0_1_0.txt` are served through the text corpus like every other container.

Accents are drawn the way the game draws them: acute and grave from the stroke the Spanish, French and Italian layouts
use, ã/õ from the font's own s turned on its side, ç from the turned apostrophe, í from the bare stem. The USA font has
no period; MÁX. and Combo Máx. take the dot of its own exclamation mark.

## Blocked

- Skill screen, Classic butterfly letters, Bookshelf book frames (368 baked book titles) and the 44 manual
  pages: not generated yet.
- Classic `Logo.mbs`: its LOGO animation spells a Spanish caption with 7 glyph sprites of the shared atlas; the boot
  logos play from the title intro, so the page is not known to be shown.
- `visual/untranslated.json` lists every blocked entry; a Portuguese (Brazil) run logs
  `Localization visual WARNING: untranslated localized asset` the first time the game reads one.
