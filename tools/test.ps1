<#
.SYNOPSIS
  Builds and runs the focused headless tests: no window, no game launch, no audio, no Internet.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. Only the test targets of the chosen groups are built, incrementally
  in the existing build folder (never cleaned); the game executable, the launcher and the video core are not rebuilt.
  A test whose target did not build fails without running, so an older executable can never pass in its place.

  Every test runs even after one fails; the exit code is non-zero when any of them failed. A step that cannot run here
  is SKIPPED with the reason; checks a test leaves out are listed as notes. Nothing is PASS unless it ran.

  The Features group also proves the title menu's Exit Game art: for every build of SupportedBuilds and every text
  language, the shipped title patch applied to the prepared original archives (read only; patch cycles run on temporary
  copies) must show the whole localized label in the Refined and Classic menus, never INSTALL, through prompt switching,
  rollback and the replacement of the 0.2.3 English layer. A build or language without prepared title archives is DATA
  UNAVAILABLE, never PASS; its structure is still proved on synthetic archives of its own data files, and art the patch
  was not made for must leave the Exit Game row off. Python 3 with numpy, scipy and Pillow is required (OSL_PYTHON names
  an interpreter; tools\setup.ps1 -InstallPython installs them into .venv); without it the step FAILS.

  The tests use synthetic data in temporary folders. The network tests talk to a loopback server of their own. The Bug
  Report service tests run in Node on a disposable in-memory database and load the exact request the launcher's own test
  built (build\cmake\clang64\bin\bug-report-request.json); that file is deleted before the Launcher group runs, so a
  request left by an earlier run is never used.
.PARAMETER Group
  Runtime, CPU, Packages, Launcher, Updater, Features, Graphics, or All (the default). Several can be given.
.PARAMETER TimeoutSeconds
  Fails a test that runs longer than this.
.EXAMPLE
  .\tools\test.ps1
  .\tools\test.ps1 -Group Launcher
  .\tools\test.ps1 -Group Updater,Runtime
#>
[CmdletBinding()]
param(
    [string[]]$Group = @('All'),
    [int]$TimeoutSeconds = 300
)

. (Join-Path $PSScriptRoot '_common.ps1')
$root = $script:RepoRoot
$buildDir = Join-Path $root 'build\cmake\clang64'
$groupOrder = @('Runtime', 'CPU', 'Packages', 'Launcher', 'Updater', 'Features', 'Graphics')
# powershell -File passes "-Group A,B" as one string.
$Group = @($Group | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
foreach ($name in $Group) {
    if ($name -ne 'All' -and $groupOrder -notcontains $name) { Stop-Tool "Unknown group '$name'. Groups: All, $($groupOrder -join ', ')." }
}

function Step([string]$GroupName, [string]$Title, [string]$Target = '', [scriptblock]$Block = $null, [string[]]$Expect = @()) {
    [pscustomobject]@{ Group = $GroupName; Title = $Title; Target = $Target; Block = $Block; Expect = $Expect }
}
function Result([string]$State, [string]$Reason = '', [string[]]$Notes = @()) {
    [pscustomobject]@{ State = $State; Reason = $Reason; Notes = $Notes }
}

# Windows PowerShell turns a native program's stderr into errors, which 'Stop' would make fatal.
function Invoke-Native([scriptblock]$Command) {
    $ErrorActionPreference = 'Continue'
    $lines = @(& $Command 2>&1 | ForEach-Object { "$_" })
    [pscustomobject]@{ Lines = $lines; Code = $LASTEXITCODE }
}

# One process with its output in order, a time limit, and the whole process tree stopped when the limit passes.
function Invoke-Captured([string]$Exe, [string[]]$Arguments = @(), [string]$WorkingDirectory = '', [int]$Seconds = 0) {
    if ($Seconds -le 0) { $Seconds = $TimeoutSeconds }
    $command = '"' + $Exe + '"' + (($Arguments | ForEach-Object { ' "' + $_ + '"' }) -join '')
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = Join-Path $env:SystemRoot 'System32\cmd.exe'
    $info.Arguments = "/d /s /c `"$command 2>&1`""
    $info.WorkingDirectory = if ($WorkingDirectory) { $WorkingDirectory } else { Split-Path $Exe -Parent }
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($info)
    $out = $process.StandardOutput.ReadToEndAsync()
    $err = $process.StandardError.ReadToEndAsync()
    $finished = $process.WaitForExit($Seconds * 1000)
    if (-not $finished) {
        & taskkill.exe /PID $process.Id /T /F 2>&1 | Out-Null
        $process.WaitForExit()
    }
    $lines = @(($out.Result + $err.Result) -split "`r?`n" | Where-Object { $_ -ne '' })
    [pscustomobject]@{ Code = $process.ExitCode; Lines = $lines; TimedOut = -not $finished }
}

function Show-Failure($Run, [int]$Most = 12) {
    $Run.Lines | Where-Object { $_ -match '^\s*(not ok|FAIL|Error|error)|FAIL|failed' } | Select-Object -First $Most | ForEach-Object { Write-Host "    $_" }
}

function Get-FailureReason($Run) {
    if ($Run.TimedOut) { return "timed out after $TimeoutSeconds s" }
    $fails = @($Run.Lines | Where-Object { $_ -match '^\s*(not ok|FAIL)' } | Select-Object -First 3 | ForEach-Object { $_.Trim() })
    if ($fails) { return $fails -join '; ' }
    return "exit code $($Run.Code)"
}

function Invoke-TestExe([string]$Target, [hashtable]$Built, [string[]]$Expect = @(), [string[]]$Arguments = @()) {
    if (-not $Built[$Target]) { return Result 'FAIL' "$Target did not build (see the build output above); the old executable was not run" }
    $exe = Join-Path $buildDir "bin\$Target.exe"
    if (-not (Test-Path -LiteralPath $exe)) { return Result 'FAIL' "$exe is missing after the build" }
    $run = Invoke-Captured $exe $Arguments
    $notes = @($run.Lines | Where-Object { $_ -match '^(skipped|not covered|note):' })
    if ($run.TimedOut -or $run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' (Get-FailureReason $run) $notes
    }
    $text = $run.Lines -join "`n"
    foreach ($pattern in $Expect) {
        if ($text -notmatch $pattern) { return Result 'FAIL' "expected output not found: $pattern" $notes }
    }
    $last = $run.Lines | Select-Object -Last 1
    if ($last) { Write-Host "    $last" -ForegroundColor DarkGray }
    return Result 'PASS' '' $notes
}

function Invoke-PowerShellScript([string]$Relative) {
    $shell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $run = Invoke-Captured $shell @('-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $root $Relative))
    if ($run.TimedOut -or $run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' (Get-FailureReason $run)
    }
    $run.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    return Result 'PASS'
}

function Invoke-PatchWorkflow {
    $temp = (Join-Path $buildDir 'patch-workflow-tests').Replace('\', '/')
    $run = Invoke-Native { & cmake "-DOSL_TEST_DIR=$temp" -P (Join-Path $root 'tests\patch_workflow_tests.cmake') }
    if ($run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' "cmake exited with $($run.Code)"
    }
    $run.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    return Result 'PASS'
}

function Invoke-ProfileTests {
    $python = Get-Python
    if (-not $python) { return Result 'FAIL' 'Python 3 not found (tools\setup.ps1 installs it)' }
    $run = Invoke-Captured $python.Exe (@($python.Args) + @('-I', (Join-Path $root 'tests\game_profiles_tests.py'))) $root
    if ($run.TimedOut -or $run.Code -ne 0) {
        $run.Lines | ForEach-Object { Write-Host "    $_" }
        return Result 'FAIL' (Get-FailureReason $run)
    }
    $run.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    return Result 'PASS'
}

$requestFile = Join-Path $buildDir 'bin\bug-report-request.json'

$titleArtRows = New-Object System.Collections.Generic.List[object]
$titleArtSeconds = 1800

function Get-TitleArtPython {
    $ErrorActionPreference = 'Continue'
    $candidates = @()
    if ($env:OSL_PYTHON) { $candidates += , @{ Exe = $env:OSL_PYTHON; Args = @() } }
    $default = Get-Python
    if ($default) { $candidates += , $default }
    $launcher = Get-Command py -ErrorAction SilentlyContinue
    if ($launcher) { $candidates += , @{ Exe = $launcher.Source; Args = @('-3') } }
    $any = Get-Command python -ErrorAction SilentlyContinue
    if ($any) { $candidates += , @{ Exe = $any.Source; Args = @() } }
    foreach ($candidate in $candidates) {
        $out = @(& $candidate.Exe @($candidate.Args) '-I' '-c' 'import numpy, scipy, PIL, sys; print(sys.executable)' 2>&1)
        if ($LASTEXITCODE -eq 0 -and $out) { return @{ Exe = "$($out[-1])".Trim(); Args = @() } }
    }
    return $null
}

function Read-TitleArtRows($Run) {
    foreach ($line in $Run.Lines) {
        if ($line -notmatch '^TITLEART\|') { continue }
        $field = $line.Split('|')
        $titleArtRows.Add([pscustomobject]@{ Build = $field[1]; Subject = $field[2]; Result = $field[3]; Source = $field[4]; Note = $field[5] })
    }
}

function Get-TitleArtFailures([int]$From) {
    for ($i = $From; $i -lt $titleArtRows.Count; $i++) {
        $row = $titleArtRows[$i]
        if ($row.Result -match '^FAIL: (.*)$') { "FAIL $($row.Build) / $($row.Subject): $($Matches[1].Trim())" }
    }
}

function Invoke-TitleArt([hashtable]$Built) {
    if (-not $Built['osl-tests']) { return Result 'FAIL' 'osl-tests did not build (see the build output above); the old executable was not run' }
    $python = Get-TitleArtPython
    if (-not $python) { return Result 'FAIL' 'Python 3 with numpy, scipy and Pillow not found; the title art contract cannot be proved (set OSL_PYTHON, or run tools\setup.ps1 -InstallPython)' }
    $first = $titleArtRows.Count
    $manifest = Join-Path $buildDir 'title-art-builds.tsv'
    $exe = Join-Path $buildDir 'bin\osl-tests.exe'
    $units = Invoke-Captured $python.Exe (@($python.Args) + @('-I', (Join-Path $root 'scripts\title_exit\test_title_exit.py'))) $root $titleArtSeconds
    $cpp = Invoke-Captured $exe @('--title-art', $manifest) '' $titleArtSeconds
    Read-TitleArtRows $cpp
    $verify = $null
    if (Test-Path -LiteralPath $manifest) {
        $verify = Invoke-Captured $python.Exe (@($python.Args) + @('-W', 'ignore', '-I', (Join-Path $root 'scripts\title_exit\verify_title_art.py'), $manifest, (Join-Path $root 'game\prepared'))) $root $titleArtSeconds
        Read-TitleArtRows $verify
    }
    $problems = @(Get-TitleArtFailures $first)
    if ($units.TimedOut -or $units.Code -ne 0) { $problems += "generator tests: $(Get-FailureReason $units)"; $units.Lines | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" } }
    if ($cpp.TimedOut -or $cpp.Code -ne 0) { $problems += "title art tests: $(Get-FailureReason $cpp)" }
    if (-not $verify) { $problems += 'the title art manifest was not written' }
    elseif ($verify.TimedOut -or $verify.Code -ne 0) { $problems += "title art verification: $(Get-FailureReason $verify)"; $verify.Lines | Where-Object { $_ -notmatch '^TITLEART\|' } | Select-Object -Last 12 | ForEach-Object { Write-Host "    $_" } }
    $summary = @($cpp.Lines | Where-Object { $_ -match '^(OK|FAILED) \(' }) | Select-Object -Last 1
    if ($summary) { Write-Host "    $summary" -ForegroundColor DarkGray }
    if ($problems) { return Result 'FAIL' ($problems -join '; ') }
    return Result 'PASS'
}

function Invoke-BugReportService {
    $node = Get-Command node -ErrorAction SilentlyContinue
    if (-not $node) { return Result 'SKIPPED' 'Node.js 22 or newer not found' }
    $haveRequest = Test-Path -LiteralPath $requestFile
    if ($haveRequest) { $env:OSL_CPP_REQUEST = $requestFile } else { Remove-Item Env:\OSL_CPP_REQUEST -ErrorAction SilentlyContinue }
    try {
        $run = Invoke-Captured $node.Source @('--test', '--test-reporter=tap', 'test/*.test.js') (Join-Path $root 'server\odin-bug-reports')
    } finally {
        Remove-Item Env:\OSL_CPP_REQUEST -ErrorAction SilentlyContinue
    }
    $summary = @($run.Lines | Where-Object { $_ -match '^# (tests|pass|fail|skipped) ' })
    $summary | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }
    if ($run.TimedOut -or $run.Code -ne 0) {
        Show-Failure $run
        return Result 'FAIL' (Get-FailureReason $run)
    }
    if (-not $haveRequest) { return Result 'FAIL' 'the launcher test did not leave its request (bin\bug-report-request.json), so the contract test did not run' }
    if ($summary -notcontains '# skipped 0') { return Result 'FAIL' 'a Worker test was skipped; the launcher contract test must run' }
    return Result 'PASS'
}

function Invoke-ReleaseHelper([hashtable]$Built) {
    $helper = Join-Path $root 'build\cmake\clang64-release\trg-updater-clang64\trg-updater.exe'
    if (-not (Test-Path -LiteralPath $helper)) { return Result 'SKIPPED' 'no release CLANG64 helper (tools\build-updater.ps1 makes it)' }
    return Invoke-TestExe 'osl-update-helper-tests' $Built @() @($helper)
}

$steps = @(
    (Step 'Runtime' 'runtime locks and the guest memory table' 'osl-runtime-tests'),
    (Step 'Runtime' 'Windows guest memory (sparse backing, commitment, aliases, release)' 'osl-memory-windows-tests'),
    (Step 'Runtime' 'run log retention and capture in a Windows-subsystem process without standard handles' 'osl-log-tests'),
    (Step 'CPU' 'MOVBE, BMI1 and LZCNT emulation and rewrite' 'osl-isa-tests'),
    (Step 'Packages' 'game profile, package identity, build parity of every supported build, layering, settings, keys, controller bindings, controller prompts' 'osl-tests' -Expect @(
        '(?m)^  CUSA05083 1\.00 USA\s+PASS\s*$', '(?m)^  CUSA05083 1\.01 USA\s+PASS\s*$', '(?m)^  CUSA01290 1\.00 Japan\s+PASS\s*$',
        '(?m)^  CUSA01290 1\.02 Japan\s+PASS\s*$', '(?m)^  CUSA05262 1\.00 Europe\s+PASS\s*$', '(?m)^  CUSA05262 1\.01 Europe\s+PASS\s*$',
        '(?m)^  CUSA03873 1\.00 HK\s+PASS\s*$', '(?m)^  CUSA03873 1\.00 Asia\s+PASS\s*$')),
    (Step 'Packages' 'unsupported-build diagnostics: read-only sources, SELF/ELF, feature states, classification, report, prompt and title assets, issue #19, local builds and packages' 'osl-build-diagnostics-tests'),
    (Step 'Packages' 'scripts\game_profiles.py: releases that share a package identity, told apart by eboot.bin' -Block { Invoke-ProfileTests }),
    (Step 'Packages' 'patch workflow (cmake script, synthetic files)' -Block { Invoke-PatchWorkflow }),
    (Step 'Launcher' 'Bug Report: JSON, service address, log choice, privacy filter, report and preview, client against a stand-in transport, My Reports, nothing sent on its own' 'osl-bug-report-tests'),
    (Step 'Launcher' 'Bug Report service (Node, disposable database): submission, receipts, limits, retention, admin, dashboard, and the launcher''s own request' -Block { Invoke-BugReportService }),
    (Step 'Launcher' 'worker.ps1 (stand-in npx): placeholders, missing files, dry run, failures; nothing deployed' -Block { Invoke-PowerShellScript 'tests\worker_script_tests.ps1' }),
    (Step 'Launcher' 'sidebar page icons and header art' 'osl-launcher-asset-tests'),
    (Step 'Launcher' 'window fit' 'osl-window-tests'),
    (Step 'Updater' 'versions, release JSON, SHA-256, ZIP safety, staging, protected game and user data, jobs, install, rollback, address policy' 'osl-update-tests'),
    (Step 'Updater' 'loopback HTTP server only: 404, 500, 504, dropped and stalled transfers, cancellation, redirects, address policy, retries, partial files' 'osl-update-network-tests'),
    (Step 'Updater' 'trg-updater.exe end to end on temporary folders: waits for its parent, installs, restarts a marker program, rolls back' 'osl-update-helper-tests'),
    (Step 'Updater' 'the same helper tests against the release CLANG64 helper' -Block { Invoke-ReleaseHelper $built }),
    (Step 'Features' 'startup notice and frame counter visibility' 'osl-notice-tests'),
    (Step 'Features' 'cursor visibility' 'osl-cursor-tests'),
    (Step 'Features' 'Frame rate pacing: modes, 30 / 60 / 90 / 120 / 144 / 165 / 240 / Unlimited, repeat cadence, VSync and display limits, absolute-slot limiter, one VideoOut path above every renderer, build and language independence' 'osl-frame-pacing-tests'),
    (Step 'Features' 'Frame rate setting for every supported build and language: launcher and video core accept the same modes, default and invalid values are 60, 120 stored and read back, reset, unrelated lines kept' 'osl-tests' -Block { Invoke-TestExe 'osl-tests' $built @('(?m)^frame rate: OK \(') @('--frame-rate') }),
    (Step 'Features' 'cheats and Experience Multiplier: hooks and live switches on the game''s own instructions (original against cheat), level-up model, session state, F10 menu logic and hotkey, local builds, coexistence, diagnostics' 'osl-cheats-tests' -Expect @(
        '(?m)^HK reference: 7 sites, 23 bytes written, 22 changed', '(?m)^Cheat build parity: \d+ build\(s\) verified on real files',
        '(?m)^  Max Gold Gain +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED', '(?m)^  Max Phozon Gain +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED',
        '(?m)^  No AP Consumption +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED', '(?m)^  Infinite PP +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED',
        '(?m)^  Infinite POW +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED', '(?m)^  Infinite Health +HOOK VERIFIED +SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED', '(?m)^  Infinite Arrows +BYTE PATCH VERIFIED  SEMANTIC MODEL VERIFIED  LIVE TOGGLE VERIFIED',
        '(?m)^  Experience Multiplier +HOOK VERIFIED \(', '(?m)^  Experience Multiplier +LEVEL MODEL VERIFIED \(', '(?m)^Experience build parity: \d+ build\(s\) verified on real files')),
    (Step 'Features' 'local trophies' 'osl-trophy-tests'),
    (Step 'Features' 'title menu Exit Game art: every supported build and language, Refined and Classic, never INSTALL on an active row' 'osl-tests' -Block { Invoke-TitleArt $built }),
    (Step 'Graphics' 'GPU tile layouts and addresses' 'osl-tiling-tests'),
    (Step 'Graphics' 'renderer selection (Auto, Vulkan, OpenGL, Direct3D 11), the Vulkan probe on runtimes 1.0 to 1.4 and without a loader, the window API and the OpenGL requirements' 'osl-renderer-selection-tests'),
    (Step 'Graphics' 'Vulkan buffer arenas without sparse binding: layouts, BDA entries, migration, write after migration, sparse path unchanged (no device)' 'osl-buffer-arena-tests'),
    (Step 'Graphics' 'OpenGL backend (detiler, emitted GLSL through glslang, no context)' 'osl-opengl-backend-tests'),
    (Step 'Graphics' 'OpenGL fast paths (write watch with real faults, state tracker, stream ring, no context)' 'osl-opengl-perf-tests'),
    (Step 'Graphics' 'ANGLE dependency (pin, cache, extraction, DLL staging, package checks, no network)' -Block { Invoke-PowerShellScript 'tests\angle_dependency_tests.ps1' })
)

$wanted = @(if ($Group -contains 'All') { $groupOrder } else { $groupOrder | Where-Object { $Group -contains $_ } })
$steps = @($steps | Where-Object { $wanted -contains $_.Group })
$targets = @($steps | Where-Object { $_.Target } | ForEach-Object { $_.Target } | Select-Object -Unique)

$tc = Resolve-Toolchain 'auto'
$env:PATH = "$($tc.Bin);$env:PATH"
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'build.ninja'))) { Stop-Tool 'Run tools\build.ps1 once first: the tests reuse its build folder.' }

$started = Get-Date
$built = @{}
if ($wanted -contains 'Launcher') { Remove-Item -LiteralPath $requestFile -ErrorAction SilentlyContinue }
if ($targets.Count) {
    $null = Sync-GpuDependencies
    Write-Step "Building $($targets.Count) test target(s) incrementally"
    $build = Invoke-Native { & ninja -C $buildDir -k 0 @targets }
    if ($build.Code -ne 0) { $build.Lines | Where-Object { $_ -notmatch 'Deprecation|cmake_minimum_required|compatibility with|^\s*$' } | ForEach-Object { Write-Host "    $_" } }
    else { $build.Lines | Select-Object -Last 1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray } }
    foreach ($target in $targets) {
        $built[$target] = $build.Code -eq 0 -or (Invoke-Native { & ninja -C $buildDir $target }).Code -eq 0
    }
}

$results = foreach ($step in $steps) {
    Write-Step "$($step.Group): $($step.Title)"
    $clock = [System.Diagnostics.Stopwatch]::StartNew()
    $result = if ($step.Block) { & $step.Block } else { Invoke-TestExe $step.Target $built $step.Expect }
    $clock.Stop()
    switch ($result.State) {
        'PASS' { Write-Host "    PASS ($([math]::Round($clock.Elapsed.TotalSeconds, 1)) s)" -ForegroundColor Green }
        'FAIL' { Write-Host "    FAIL: $($result.Reason)" -ForegroundColor Red }
        default { Write-Host "    SKIPPED: $($result.Reason)" -ForegroundColor Yellow }
    }
    [pscustomobject]@{ Step = $step; Result = $result; Seconds = $clock.Elapsed.TotalSeconds }
}

Write-Host ''
Write-Host 'Odin Sphere Leifthrasir - Test Suite' -ForegroundColor Cyan
Write-Host ''
$counts = @{ PASS = 0; FAIL = 0; SKIPPED = 0; PARTIAL = 0 }
foreach ($name in $wanted) {
    $mine = @($results | Where-Object { $_.Step.Group -eq $name })
    $failed = @($mine | Where-Object { $_.Result.State -eq 'FAIL' })
    $skipped = @($mine | Where-Object { $_.Result.State -eq 'SKIPPED' })
    $state = if ($failed) { 'FAIL' } elseif ($skipped.Count -eq $mine.Count) { 'SKIPPED' } elseif ($skipped) { 'PARTIAL' } else { 'PASS' }
    $counts[$state]++
    $seconds = ($mine | Measure-Object -Property Seconds -Sum).Sum
    $color = @{ PASS = 'Green'; FAIL = 'Red'; SKIPPED = 'Yellow'; PARTIAL = 'Yellow' }[$state]
    Write-Host ('{0,-12}{1,-9}{2,6:N1} s' -f $name, $state, $seconds) -ForegroundColor $color
    foreach ($entry in $failed) { Write-Host "    failed: $($entry.Step.Title): $($entry.Result.Reason)" -ForegroundColor Red }
    foreach ($entry in $skipped) { Write-Host "    skipped: $($entry.Step.Title): $($entry.Result.Reason)" -ForegroundColor Yellow }
    foreach ($entry in $mine) { foreach ($note in $entry.Result.Notes) { Write-Host "    note: $note" -ForegroundColor DarkGray } }
}
if ($titleArtRows.Count) {
    Write-Host ''
    Write-Host 'Title menu Exit Game art (real files = prepared archives read; DATA UNAVAILABLE is never PASS)' -ForegroundColor Cyan
    foreach ($build in @($titleArtRows | ForEach-Object { $_.Build } | Select-Object -Unique)) {
        Write-Host "  $build"
        foreach ($row in @($titleArtRows | Where-Object { $_.Build -eq $build })) {
            $color = if ($row.Result -match '^FAIL') { 'Red' } elseif ($row.Result -match 'UNAVAILABLE') { 'Yellow' } else { 'Gray' }
            $detail = (@($row.Source, $row.Note) | Where-Object { $_ }) -join '; '
            Write-Host ('    {0,-34}{1,-18}[{2}]' -f $row.Subject, $row.Result, $detail) -ForegroundColor $color
        }
    }
}
Write-Host ''
Write-Host "Total: $($wanted.Count) groups"
Write-Host "Passed: $($counts.PASS)"
if ($counts.PARTIAL) { Write-Host "Passed with skipped steps: $($counts.PARTIAL)" }
Write-Host "Failed: $($counts.FAIL)"
Write-Host "Skipped: $($counts.SKIPPED)"
Write-Host ("Duration: {0:N1} s" -f ((Get-Date) - $started).TotalSeconds)
if ($counts.FAIL) { exit 1 }
exit 0
