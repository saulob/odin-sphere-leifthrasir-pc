# Shared helpers, dot-sourced by the other tools. Works in Windows PowerShell 5.1 and PowerShell 7.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$script:RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path

function Write-Step([string]$Text) { Write-Host "==> $Text" -ForegroundColor Cyan }
function Write-Ok([string]$Text)   { Write-Host "  [ok]   $Text" -ForegroundColor Green }
function Write-Warn([string]$Text) { Write-Host "  [warn] $Text" -ForegroundColor Yellow }
function Write-Bad([string]$Text)  { Write-Host "  [miss] $Text" -ForegroundColor Red }

function Stop-Tool([string]$Text) {
    Write-Host "error: $Text" -ForegroundColor Red
    exit 1
}

function Get-VenvPython {
    $path = Join-Path $script:RepoRoot '.venv\Scripts\python.exe'
    if (Test-Path -LiteralPath $path) { return $path }
    return $null
}

# Interpreter for the scripts: the project venv, else the py launcher, else the first python on PATH.
function Get-Python {
    $venv = Get-VenvPython
    if ($venv) { return @{ Exe = $venv; Args = @() } }
    $launcher = Get-Command py -ErrorAction SilentlyContinue
    if ($launcher) { return @{ Exe = $launcher.Source; Args = @('-3') } }
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) { return @{ Exe = $python.Source; Args = @() } }
    return $null
}

# Runs scripts/<name> with isolated-mode Python so nothing next to the data is imported.
function Invoke-Script([string]$Name, [string[]]$Arguments = @()) {
    $python = Get-Python
    if (-not $python) { Stop-Tool 'Python 3 not found. Run tools\setup.ps1.' }
    $script = Join-Path $script:RepoRoot "scripts\$Name"
    & $python.Exe @($python.Args) '-I' $script @Arguments | Out-Host
    return $LASTEXITCODE
}

function Test-PythonModule([string]$Module) {
    $python = Get-Python
    if (-not $python) { return $false }
    $null = & $python.Exe @($python.Args) '-I' '-c' "import $Module" 2>&1
    return ($LASTEXITCODE -eq 0)
}

function Get-Msys2Root {
    $candidates = @($env:MSYS2_ROOT, 'C:\msys64', 'D:\msys64') | Where-Object { $_ }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath (Join-Path $candidate 'usr\bin\pacman.exe')) { return $candidate }
    }
    return $null
}

# The only supported toolchain: MSYS2 CLANG64, as in the reference ports (the video core needs it).
$script:Toolchains = [ordered]@{
    clang64 = @{ Dir = 'clang64'; Compiler = 'clang.exe'; Package = 'mingw-w64-clang-x86_64' }
}
# Needed to build the loader, runtime and video core.
$script:BuildTools = @('clang.exe', 'cmake.exe', 'ninja.exe', 'pkgconf.exe', 'SDL3.dll', 'glslang.exe', 'vulkan-1.dll', 'libfreetype-6.dll')
$script:Clang64Packages = 'mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,sdl3,freetype} ' +
    'mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} ' +
    'mingw-w64-clang-x86_64-{fmt,boost,robin-map,xxhash,libpng,zydis,spirv-tools,spirv-headers,spirv-cross}'

function Get-ToolchainStatus([string]$Name) {
    $info = $script:Toolchains[$Name]
    $root = Get-Msys2Root
    $status = @{ Name = $Name; Ready = $false; Bin = $null; Missing = @() }
    if (-not $root) { $status.Missing = @('MSYS2'); return $status }
    $bin = Join-Path $root "$($info.Dir)\bin"
    $status.Bin = $bin
    foreach ($tool in $script:BuildTools) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $tool))) { $status.Missing += $tool }
    }
    $status.Ready = ($status.Missing.Count -eq 0)
    return $status
}

function Resolve-Toolchain([string]$Requested) {
    if ($Requested -and $Requested -ne 'auto') {
        $status = Get-ToolchainStatus $Requested
        if (-not $status.Ready) { Stop-Tool "Toolchain '$Requested' is incomplete (missing: $($status.Missing -join ', ')). Run tools\setup.ps1." }
        return $status
    }
    foreach ($name in $script:Toolchains.Keys) {
        $status = Get-ToolchainStatus $name
        if ($status.Ready) { return $status }
    }
    Stop-Tool 'MSYS2 CLANG64 toolchain or libraries missing. Run tools\setup.ps1 for what to install.'
}

# Fetches the pinned sources in gpu\deps.txt that are missing or not at their commit, and returns their paths. A fetched
# folder stays pristine and holds its commit in .osl_commit; CMake builds the patched copies (cmake\OslPinnedSources.cmake).
function Sync-GpuDependencies {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) { Stop-Tool 'git is needed to fetch the video core dependencies (gpu\deps.txt).' }
    $gpu = Join-Path $script:RepoRoot 'gpu'
    $fetched = @()
    foreach ($line in Get-Content -LiteralPath (Join-Path $gpu 'deps.txt')) {
        if ($line -match '^\s*(#|$)') { continue }
        $path, $url, $commit = -split $line
        $dir = Join-Path $gpu $path
        $stamp = Join-Path $dir '.osl_commit'
        $recorded = if (Test-Path -LiteralPath $stamp) { (Get-Content -LiteralPath $stamp -Raw).Trim() } else { $null }
        if ($recorded -eq $commit) { continue }
        $reason = if ($null -eq $recorded) { 'not fetched yet' } else { 'not the pristine pinned checkout' }
        Write-Step "Fetching $path ($($commit.Substring(0, 7))): $reason"
        if (Test-Path -LiteralPath $dir) { Remove-Item -LiteralPath $dir -Recurse -Force }
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
        & git -C $dir init --quiet
        & git -C $dir fetch --quiet --depth 1 $url $commit
        if ($LASTEXITCODE -ne 0) { Stop-Tool "git fetch failed for $url" }
        & git -C $dir -c advice.detachedHead=false checkout --quiet FETCH_HEAD
        if (Test-Path -LiteralPath (Join-Path $dir '.gitmodules')) {
            & git -C $dir submodule update --quiet --init --recursive --depth 1
            if ($LASTEXITCODE -ne 0) { Stop-Tool "submodules of $path failed" }
        }
        [System.IO.File]::WriteAllText($stamp, $commit, [System.Text.Encoding]::ASCII)
        $fetched += $path
    }
    return $fetched
}
