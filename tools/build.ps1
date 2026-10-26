<#
.SYNOPSIS
  Builds osl-loader.exe incrementally (MSYS2 CLANG64, CMake + Ninja). -Run launches it.
.DESCRIPTION
  Reuses what is current and updates only what is stale: the video core dependencies (gpu\deps.txt), the CMake
  configuration and, once the game is installed, the boot image of the active prepared build. The one build
  configuration, RelWithDebInfo (-O2 -g -DNDEBUG), is what is tested and what a package must contain; nothing else
  is built. The game is installed from the launcher (Play page, Install game...), as in a release.
.PARAMETER Version
  The prepared build whose boot image to update (default: the active one, game\prepared\CUSA05083-1.00 by default).
.PARAMETER Run
  Launches the loader that was just built and stays attached until the game is closed. The launcher opens first
  when its rules say so (docs\LAUNCHER.md). Logs to build\logs\odin_sphere_leifthrasir_NNN.log.
.PARAMETER Clean
  Deletes build\cmake first (the installed game, the boot image and the logs are kept).
.PARAMETER CheckImports
  Diagnostic: with -Run, lists the PS4 imports the runtime lacks instead of running the game.
.PARAMETER TimeoutSeconds
  Diagnostic for automated tests only: with -Run, kills the loader after this many seconds (and skips the launcher).
.PARAMETER LoaderArgs
  Extra arguments for osl-loader.exe (with -Run).
.EXAMPLE
  .\tools\build.ps1
  .\tools\build.ps1 -Run
#>
[CmdletBinding()]
param(
    [switch]$Run,
    [switch]$Clean,
    [switch]$CheckImports,
    [string]$Version = '',
    [int]$TimeoutSeconds = 0,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$LoaderArgs = @()
)

. (Join-Path $PSScriptRoot '_common.ps1')
$config = 'RelWithDebInfo'
$root = $script:RepoRoot
$buildDir = Join-Path $root 'build\cmake\clang64'
if (-not $Version) {
    $views = @(Get-ChildItem -LiteralPath (Join-Path $root 'game\prepared') -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName '.osl_view.json') } |
        ForEach-Object {
            [pscustomobject]@{ Name = $_.Name; Time = (Get-Item -LiteralPath (Join-Path $_.FullName '.osl_view.json')).LastWriteTime }
        } | Sort-Object -Property Time -Descending)
    $Version = if ($views.Count -gt 0) { $views[0].Name } else { 'CUSA05083-1.00' }
}
$loader = Join-Path $buildDir 'bin\osl-loader.exe'
$view = Join-Path $root "game\prepared\$Version"
$marker = Join-Path $view '.osl_view.json'
$image = Join-Path $root "build\image\$Version"
$loadExecExit = 30 # osl-loader: sceSystemServiceLoadExec ended the process

$imageStamp = Join-Path $image '.osl_image_inputs'

# What the boot image is made from: the installation and the builder's sources (by content).
function Get-ImageInputs {
    $lines = @("installed $((Get-Item -LiteralPath $marker).LastWriteTimeUtc.Ticks)")
    foreach ($source in 'launcher\boot_image.cpp', 'launcher\boot_image.h', 'launcher\game_files.cpp',
        'launcher\game_files.h', 'launcher\game_profile.h') {
        $lines += "$((Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $root $source)).Hash) $source"
    }
    return ($lines -join "`n")
}

# Why the boot image must be rebuilt, or $null when it is current.
function Get-ImageProblem {
    foreach ($name in 'boot.bin', 'image.json') {
        if (-not (Test-Path -LiteralPath (Join-Path $image $name))) { return "no $name yet" }
    }
    if (-not (Test-Path -LiteralPath $imageStamp)) { return 'built without a record of its inputs' }
    if ((Get-Content -LiteralPath $imageStamp -Raw) -cne (Get-ImageInputs)) { return 'the game was installed again or the builder changed' }
    return $null
}

$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
Write-Step "Toolchain clang64 ($($tc.Bin)), $config"

$fetched = @(Sync-GpuDependencies)
if ($fetched.Count -eq 0) { Write-Step 'Pinned sources (gpu\deps.txt): current' }

if ($Clean -and (Test-Path -LiteralPath $buildDir)) {
    Write-Step 'Cleaning build\cmake'
    Remove-Item -LiteralPath $buildDir -Recurse -Force
}
$cache = Join-Path $buildDir 'CMakeCache.txt'
$configured = (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja')) -and (Test-Path -LiteralPath $cache) -and
    (Select-String -Path $cache -SimpleMatch "CMAKE_BUILD_TYPE:STRING=$config" -Quiet)
if (-not $configured) {
    Write-Step 'Configuring'
    & cmake -S $root -B $buildDir -G Ninja "-DCMAKE_BUILD_TYPE=$config" '-DCMAKE_C_COMPILER=clang'
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'CMake configuration failed' }
}
# Ninja re-runs CMake first when a CMake input changed (CMakeLists, gpu\deps.txt, a fetched commit, a patch).
Write-Step 'Building incrementally'
& ninja -C $buildDir osl-loader
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Build failed' }
if (-not (Test-Path -LiteralPath $loader)) { Stop-Tool "Missing $loader" }

$imageProblem = if (Test-Path -LiteralPath $marker) { Get-ImageProblem } else { $null }
if (-not (Test-Path -LiteralPath $marker)) {
    Write-Warn 'The game is not installed yet: start the game and use Install game... on the launcher''s Play page.'
}
elseif ($imageProblem) {
    Write-Step "Updating the boot image ($Version): $imageProblem"
    if (Test-Path -LiteralPath $imageStamp) { Remove-Item -LiteralPath $imageStamp }
    & $loader --prepare-image $view $image
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'Boot image build failed' }
    [System.IO.File]::WriteAllText($imageStamp, (Get-ImageInputs), [System.Text.Encoding]::ASCII)
}
else {
    Write-Step "Boot image ($Version): current"
}
Write-Host ''
Write-Host "Built $loader" -ForegroundColor Green
if (-not $Run) { return }

$running = @(Get-Process osl-loader -ErrorAction SilentlyContinue | Where-Object { $_.Path -eq $loader })
if ($running.Count -gt 0) {
    Write-Warn "osl-loader.exe is already running (PID $($running.Id -join ', ')); not launching another copy."
    return
}
# The loader finds the game, the boot image, user\ and build\logs by itself in this tree.
$arguments = @()
if ($CheckImports) { $arguments += '--check-imports' }
if ($TimeoutSeconds -gt 0) { $arguments += '--osl-skip-launcher' }
$arguments += $LoaderArgs
Push-Location (Split-Path $loader -Parent)
try {
    if ($TimeoutSeconds -gt 0) {
        $process = Start-Process -FilePath $loader -ArgumentList $arguments -NoNewWindow -PassThru
        $null = $process.Handle # keeps the exit code readable after the process ends
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            Write-Warn "Stopping osl-loader.exe after $TimeoutSeconds seconds"
            $process.Kill()
            $process.WaitForExit()
        }
        $code = $process.ExitCode
    }
    else {
        & $loader @arguments
        $code = $LASTEXITCODE
    }
}
finally {
    Pop-Location
}
Write-Host "osl-loader.exe exit code $code"
if ($code -eq $loadExecExit) { Write-Host 'The game requested sceSystemServiceLoadExec (see the LoadExec lines above).' }
exit $code
