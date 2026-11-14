<#
.SYNOPSIS
  Builds and runs the focused headless tests: no window, no game launch, no audio, no Internet.
.DESCRIPTION
  Needs the build configured once by tools\build.ps1. Only the test targets of the chosen groups are built, incrementally
  in the existing build folder (never cleaned); the game executable, the launcher and the video core are not rebuilt.
  A test whose target did not build fails without running, so an older executable can never pass in its place.

  Every test runs even after one fails; the exit code is non-zero when any of them failed. A step that cannot run here
  is SKIPPED with the reason; checks a test leaves out are listed as notes. Nothing is PASS unless it ran.

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
function Invoke-Captured([string]$Exe, [string[]]$Arguments = @(), [string]$WorkingDirectory = '') {
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
    $finished = $process.WaitForExit($TimeoutSeconds * 1000)
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

$requestFile = Join-Path $buildDir 'bin\bug-report-request.json'

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
    (Step 'Packages' 'game profile, package identity, build parity of every supported build, layering, settings, keys, controller prompts' 'osl-tests' -Expect @(
        '(?m)^  CUSA05083 1\.00 USA\s+PASS\s*$', '(?m)^  CUSA05083 1\.01 USA\s+PASS\s*$', '(?m)^  CUSA01290 1\.00 Japan\s+PASS\s*$',
        '(?m)^  CUSA01290 1\.02 Japan\s+PASS\s*$', '(?m)^  CUSA05262 1\.00 Europe\s+PASS\s*$', '(?m)^  CUSA05262 1\.01 Europe\s+PASS\s*$',
        '(?m)^  CUSA03873 1\.00 HK\s+PASS\s*$')),
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
    (Step 'Features' 'local trophies' 'osl-trophy-tests'),
    (Step 'Graphics' 'GPU tile layouts and addresses' 'osl-tiling-tests')
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
Write-Host ''
Write-Host "Total: $($wanted.Count) groups"
Write-Host "Passed: $($counts.PASS)"
if ($counts.PARTIAL) { Write-Host "Passed with skipped steps: $($counts.PARTIAL)" }
Write-Host "Failed: $($counts.FAIL)"
Write-Host "Skipped: $($counts.SKIPPED)"
Write-Host ("Duration: {0:N1} s" -f ((Get-Date) - $started).TotalSeconds)
if ($counts.FAIL) { exit 1 }
exit 0
