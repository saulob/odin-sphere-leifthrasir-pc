<#
.SYNOPSIS
  Developer convenience: unpacks the fake package from game\ (resumable) into the prepared /app0 tree.
.DESCRIPTION
  game\extracted\<folder>                       what each package contains (the .pkg files are only read)
  game\prepared\<folder>                        the /app0 tree (hard links, no copies)
  Needs Python with the cryptography package (tools\setup.ps1 -InstallPython). tools\build.ps1 then builds the boot
  image. Players and the launcher do not use this: Install game... unpacks the .pkg natively.
.PARAMETER Title
  CUSA05083 (USA). Empty: the title of the packages.
.PARAMETER Version
  1.00. Empty: the one build the packages complete.
.PARAMETER Scope
  core: executables, modules and metadata only (about 25 MB). full: all game data (about 3.3 GB, the CPK archives).
#>
[CmdletBinding()]
param(
    [ValidateSet('', 'CUSA05083')][string]$Title = '',
    [ValidateSet('', '1.00')][string]$Version = '',
    [ValidateSet('core', 'full')][string]$Scope = 'full',
    [string]$GameRoot
)

. (Join-Path $PSScriptRoot '_common.ps1')
if (-not $GameRoot) { $GameRoot = Join-Path $script:RepoRoot 'game' }
if (-not (Test-PythonModule 'cryptography')) { Stop-Tool 'cryptography is missing: run tools\setup.ps1 -InstallPython' }

$selectArgs = @()
if ($Title) { $selectArgs += @('--title', $Title) }
if ($Version) { $selectArgs += @('--version', $Version) }
Write-Step "Unpacking and layering ($(if ($Title) { $Title } else { 'the packages' }), $Scope)"
$code = Invoke-Script 'prepare_game.py' (@('--game-root', $GameRoot) + $selectArgs + @('--scope', $Scope))
if ($code -ne 0) { exit $code }
Write-Ok 'Prepared. tools\build.ps1 builds the boot image.'
