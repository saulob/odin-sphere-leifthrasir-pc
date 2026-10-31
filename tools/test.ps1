<#
.SYNOPSIS
  Builds and runs the focused tests: the game profile and the package identity, the build parity of every supported build
  (identity, languages, display, graphics, gameplay, controls, trophies, save data and runtime patches, checked on
  synthetic fixtures without game files; the script requires a PASS row for each build, CUSA05083 1.01 USA included),
  the layering of an update over its base, the settings file, the run log's
  retention and capture without a console, the keyboard bindings, the controller prompts patch, the cursor visibility rules, the window fit, the GPU tile layouts, the patch workflow and the
  launcher self-update (versions, release JSON, checksums, ZIP safety, the install transaction and the real update helper). They use
  synthetic data in a temporary folder: no window, no game files.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. The tests are not part of the default build. The update helper
  tests also run against the release CLANG64 trg-updater.exe when tools\build-updater.ps1 has made one.
#>
[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot '_common.ps1')
$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
$buildDir = Join-Path $script:RepoRoot 'build\cmake\clang64'
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) { Stop-Tool 'Run tools\build.ps1 first: it configures the build.' }
$null = Sync-GpuDependencies

Write-Step 'Running the patch workflow tests'
& cmake "-DOSL_TEST_DIR=$((Join-Path $buildDir 'patch-workflow-tests').Replace('\', '/'))" -P (Join-Path $script:RepoRoot 'tests\patch_workflow_tests.cmake')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Building the tests'
& ninja -C $buildDir osl-memory-windows-tests osl-tests osl-log-tests osl-window-tests osl-tiling-tests osl-cursor-tests osl-notice-tests osl-trophy-tests osl-runtime-tests osl-isa-tests osl-update-tests osl-update-helper-tests
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Test build failed' }

Write-Step 'Running the Windows memory tests (sparse backing, commitment, aliases, release)'
& (Join-Path $buildDir 'bin\osl-memory-windows-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the game, build parity, settings and keys tests'
$gameTests = (& (Join-Path $buildDir 'bin\osl-tests.exe') | Out-String)
$gameTestsExit = $LASTEXITCODE
Write-Host $gameTests
if ($gameTestsExit -ne 0) { exit $gameTestsExit }
foreach ($label in 'CUSA05083 1.00 USA', 'CUSA05083 1.01 USA', 'CUSA01290 1.00 Japan', 'CUSA01290 1.02 Japan', 'CUSA05262 1.00 Europe', 'CUSA05262 1.01 Europe', 'CUSA03873 1.00 HK') {
    if ($gameTests -notmatch "(?m)^  $([regex]::Escape($label))\s+PASS\s*$") { Stop-Tool "Build parity has no PASS row for $label" }
}

Write-Step 'Running the log tests (retention, and capture in a Windows-subsystem process without standard handles)'
& (Join-Path $buildDir 'bin\osl-log-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the trophy tests'
& (Join-Path $buildDir 'bin\osl-trophy-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the window fit tests'
& (Join-Path $buildDir 'bin\osl-window-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the cursor visibility tests'
& (Join-Path $buildDir 'bin\osl-cursor-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the startup notice and frame counter visibility tests'
& (Join-Path $buildDir 'bin\osl-notice-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the runtime lock and guest memory table tests'
& (Join-Path $buildDir 'bin\osl-runtime-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the MOVBE, BMI1 and LZCNT tests'
& (Join-Path $buildDir 'bin\osl-isa-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the tiling tests (GPU tile layouts and addresses)'
& (Join-Path $buildDir 'bin\osl-tiling-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the updater tests'
& (Join-Path $buildDir 'bin\osl-update-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Step 'Running the update helper tests (trg-updater.exe alone in a temporary folder)'
& (Join-Path $buildDir 'bin\osl-update-helper-tests.exe')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$releaseUpdater = Join-Path $script:RepoRoot 'build\cmake\clang64-release\trg-updater-clang64\trg-updater.exe'
if (Test-Path -LiteralPath $releaseUpdater) {
    Write-Step "Running the update helper tests against the release CLANG64 build ($releaseUpdater)"
    & (Join-Path $buildDir 'bin\osl-update-helper-tests.exe') $releaseUpdater
}
exit $LASTEXITCODE
