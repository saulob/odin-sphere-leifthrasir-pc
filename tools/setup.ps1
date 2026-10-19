<#
.SYNOPSIS
  Checks the prerequisites for building the project and says what is missing.
.PARAMETER InstallPython
  Creates .venv and installs the one Python package tools\prepare-game.ps1 needs (cryptography). Optional: only
  for unpacking a PKG during development; the build and the launcher do not use Python.
  Nothing else is installed by this script.
#>
[CmdletBinding()]
param([switch]$InstallPython)

. (Join-Path $PSScriptRoot '_common.ps1')
$problems = 0

Write-Step 'Python (optional: tools\prepare-game.ps1 and the scripts\ tools)'
if ($InstallPython) {
    $python = Get-Python
    if (-not $python) { Stop-Tool 'Python 3.10+ is required to create the environment: https://www.python.org/downloads/' }
    if (-not (Get-VenvPython)) {
        Write-Host '  creating .venv'
        & $python.Exe @($python.Args) -m venv (Join-Path $script:RepoRoot '.venv')
        if ($LASTEXITCODE -ne 0) { Stop-Tool 'Could not create .venv' }
    }
    Write-Host '  installing cryptography into .venv'
    & (Get-VenvPython) -m pip install --quiet --disable-pip-version-check cryptography
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'pip install cryptography failed' }
}
$python = Get-Python
if (-not $python) {
    Write-Warn 'Python 3 not found (only needed for tools\prepare-game.ps1)'
} else {
    $version = ((& $python.Exe @($python.Args) '-I' '-c' 'import platform;print(platform.python_version())') | Select-Object -First 1).Trim()
    if ([version]$version -lt [version]'3.10') { Write-Warn "Python $version is too old for the scripts (need 3.10+)" }
    else { Write-Ok "Python $version ($($python.Exe))" }
    if (Test-PythonModule 'cryptography') { Write-Ok 'cryptography (PKG extraction)' }
    else { Write-Warn 'cryptography missing (tools\setup.ps1 -InstallPython adds it for tools\prepare-game.ps1)' }
}

Write-Step 'Git'
$git = Get-Command git -ErrorAction SilentlyContinue
if ($git) { Write-Ok $git.Source } else { Write-Warn 'git not found (only needed to work on the sources)' }

Write-Step 'Native toolchain (MSYS2 CLANG64)'
$root = Get-Msys2Root
if (-not $root) {
    Write-Bad 'MSYS2 not found (winget install MSYS2.MSYS2, or set MSYS2_ROOT)'; $problems++
} else {
    Write-Ok "MSYS2 at $root"
    $status = Get-ToolchainStatus 'clang64'
    if ($status.Ready) {
        $version = (& (Join-Path $status.Bin 'clang.exe') --version | Select-Object -First 1)
        Write-Ok "clang, CMake, Ninja, pkgconf, SDL3, FreeType, Vulkan loader, glslang ($($status.Bin)): $version"
    } else {
        Write-Bad "CLANG64 incomplete, missing: $($status.Missing -join ', ')"; $problems++
        Write-Host '         Install from an "MSYS2 CLANG64" shell:'
        Write-Host "         pacman -S --needed $script:Clang64Packages"
    }
}

Write-Step 'Game package (optional, never modified)'
$gameRoot = Join-Path $script:RepoRoot 'game'
$pkgs = @()
if (Test-Path -LiteralPath $gameRoot) { $pkgs = @(Get-ChildItem -LiteralPath $gameRoot -Filter '*.pkg') }
if ($pkgs.Count -gt 0) { foreach ($pkg in $pkgs) { Write-Ok ('game\{0} ({1:N1} GB)' -f $pkg.Name, ($pkg.Length / 1GB)) } }
else { Write-Ok 'no .pkg in game\ (not needed: the launcher installs the game from a .pkg anywhere)' }
$drive = Get-PSDrive -Name ([IO.Path]::GetPathRoot($script:RepoRoot).Substring(0, 1))
$freeGb = [math]::Round($drive.Free / 1GB)
if ($freeGb -ge 8) { Write-Ok "$freeGb GB free on $($drive.Name): (full extraction needs about 4 GB)" }
else { Write-Warn "$freeGb GB free on $($drive.Name): full extraction needs about 4 GB (core scope needs 25 MB)" }

Write-Host ''
if ($problems -gt 0) { Write-Host "$problems required item(s) missing." -ForegroundColor Red; exit 1 }
Write-Host 'Ready.' -ForegroundColor Green
