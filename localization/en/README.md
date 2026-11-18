# English on every build

English is listed on every build under the key `english`. The USA and European builds play their own English slot.
Japan, Hong Kong and Asia have none: the port serves the English of `sheets/` over the native slot, read from the
USA 1.00 game, so the player needs no other game.

| Path | Contents |
| --- | --- |
| `sheets/<kind>.json` | Every unit of every text resource: 79 message sheets, 248 story files, 133 ArchiveViewer pages, 2 credits |
| `sources/US.json` | The 16-hex SHA-256 prefix of every English unit (not embedded) |
| `sources/JP.json` | For each Japanese record proven to be an English record, the hash of its stored bytes; `from` names the English record when it is not the one of the same index; `excluded` and `whole` say why the others are not |

A Japanese record takes the English when the resource has as many records as the English, the printf conversions,
button glyphs and numbers agree, and the two records before and after it have the same shape. An ArchiveViewer page
is matched block by block: a text block takes the English block at the same page, frame, kind and order, and the page
keeps its own layout commands. A slot without a map (HK, Asia) applies the record proof at run time and never takes
ArchiveViewer pages or Shift-JIS text. English letters the slot's fonts cannot draw (`é` of Consommé on Japan) keep the
record in the slot's text: only Portuguese (Brazil) adds letters to the fonts.

| Build | English |
| --- | --- |
| USA 1.00, Europe 1.00, 1.01 | native, 14,925 records |
| Japan 1.02 | 14,098 / 14,925 served; 827 keep Japanese |
| Japan 1.00 | 9,311 / 14,925 served (three resources predate the 1.02 alignment) |
| USA 1.01, HK, Asia | not measured: no local files |

```
python scripts/localization/english.py generate   # sheets/ and sources/US.json from game/prepared/CUSA05083-1.00
python scripts/localization/english.py sources    # sources/JP.json from the prepared Japanese games
python scripts/localization/english.py validate
python scripts/localization/english.py parity
python scripts/localization/odin_text.py roundtrip game/prepared/<build>...
```
