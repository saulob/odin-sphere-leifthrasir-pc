<#
.SYNOPSIS
  Builds and runs the focused tests: the game profile and the package identity, the settings file, the keyboard
  bindings, the window fit and the GPU tile layouts. They use synthetic data in a temporary folder: no window,
  no game files.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. The tests are not part of the default build.
#>
[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot '_common.ps1')
$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
$buildDir = Join-Path $script:RepoRoot 'build\cmake\clang64'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) { Stop-Tool 'Run tools\build.ps1 first: it configures the build.' }

Write-Step 'Building the tests'
& ninja -C $buildDir osl-tests osl-window-tests osl-tiling-tests
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Test build failed' }

Write-Step 'Running the game, settings and keys tests'
& (Join-Path $buildDir 'bin\osl-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the window fit tests'
& (Join-Path $buildDir 'bin\osl-window-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the tiling tests (GPU tile layouts and addresses)'
& (Join-Path $buildDir 'bin\osl-tiling-tests.exe')
exit $LASTEXITCODE
