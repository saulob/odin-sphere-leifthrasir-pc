# Launcher header art

`header-art.svg` is the source of truth. Edit it first.

`header-art.png` is the runtime asset, generated from the SVG at 3840x432 (viewBox 1600x180) with a transparent
background. Do not edit it by hand; regenerate it after every SVG change.

From the repository root on Windows, with Edge installed (Edge needs an absolute output path; the pipe makes
PowerShell wait for it):

```powershell
$dir = (Resolve-Path assets/launcher).Path
& 'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe' `
  --headless=new --disable-gpu --hide-scrollbars --default-background-color=00000000 `
  --window-size=3840,432 "--screenshot=$dir\header-art.png" `
  "file:///$("$dir\header-art.svg" -replace '\\','/')" 2>&1 | Out-Null
```

## Palette

All colors are in the SVG's `<style>` block; each color has a fill class, and some also have `-line` (stroke) and
`-stop` (gradient) classes. To change a color, replace its hex in every rule that uses it.

| Class       | Color     | Use                                       |
| ----------- | --------- | ----------------------------------------- |
| `deep`      | `#170C2E` | near silhouettes, moon shadow             |
| `violet`    | `#5B3FB0` | far forest, ruins, sky wash               |
| `purple`    | `#8A63E6` | lunar arcs, moon edge, mist               |
| `blue`      | `#7C8CFF` | flower, bloom                             |
| `magenta`   | `#C23C9A` | flower, petals, warm wash                 |
| `rose`      | `#D12A78` | the launcher accent: windows, petals      |
| `highlight` | `#E8DEFF` | moon, stars, filigree                     |
| `ice`       | `#B9D4FF` | small cool highlights                     |

Keep the left ~55% of the viewBox (x < 880) almost empty: the title and subtitle are drawn there.
