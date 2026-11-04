# Release security checks

Run these on the final artifacts, after the last package run. Repackaging changes the ZIP and AppImage hashes, so a
scan only counts for the exact file that gets published.

A clean ZIP scan does not cover the programs inside it: the v0.2.2 ZIP scanned clean while Microsoft flagged its
`odin_sphere_leifthrasir.exe`. Scan each file below on its own. Uploads stay manual.

| File | Why |
| --- | --- |
| `OdinSphereLeifthrasirPC-v<version>-windows-x64.zip` | What Windows players download |
| `odin_sphere_leifthrasir.exe`, from the staged folder of that ZIP | The program Windows players run |
| `trg-updater.exe`, from the same staged folder | The helper the launcher starts to update itself |
| `OdinSphereLeifthrasirPC-v<version>-x86_64.AppImage` | What Linux players download |

`tools\package.ps1` prints the ZIP, `odin_sphere_leifthrasir.exe` and `trg-updater.exe` hashes when it finishes. Check
that each file you upload has the hash it printed.

## Release report

Record all eight values before publishing. The release notes' Security table lists every file.

| File | SHA-256 | VirusTotal URL |
| --- | --- | --- |
| Windows ZIP | | |
| `odin_sphere_leifthrasir.exe` | | |
| `trg-updater.exe` | | |
| Linux AppImage | | |

## Before publishing

Do not publish while an engine flags any of these files, unless the detection has been investigated to a conclusion
and that conclusion is written down.

Never work around a detection by packing, obfuscating or encrypting the program, by disabling a security product or
by asking players to add an exclusion.
