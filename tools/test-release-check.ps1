<#
.SYNOPSIS
  Checks that tools\package.ps1 -VerifyOnly accepts the staged release folder and refuses it once it holds the
  player's files, game data, logs, hang reports, crash dumps, symbols, build or Python leftovers, sources, test
  programs, updater leftovers, debugging tools, development markers, stale names or local paths, or lacks the update
  helper, a DLL, the licences, the header art or the program's version resource, or when its trg-updater.exe is not
  the stripped tools\build-updater.ps1 build. Nothing is built, launched or published.
.DESCRIPTION
  Needs the staged folder of tools\package.ps1 (dist\OdinSphereLeifthrasirPC-v<version>-windows-x64). Each case is a
  copy made of hard links in build\release-check-tests; a file a case changes is replaced, never written through
  its link, so the staged folder stays as it was. A case whose source file does not exist here is skipped.
#>
[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot '_common.ps1')

$root = $script:RepoRoot
$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^set\(OSL_PORT_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)') { Stop-Tool 'OSL_PORT_VERSION not found in CMakeLists.txt' }
$release = Join-Path $root "dist\OdinSphereLeifthrasirPC-v$($Matches[1])-windows-x64"
if (-not (Test-Path -LiteralPath (Join-Path $release 'odin_sphere_leifthrasir.exe'))) { Stop-Tool "Run tools\package.ps1 first: $release is missing" }
$checker = Join-Path $PSScriptRoot 'package.ps1'
$casesRoot = Join-Path $root 'build\release-check-tests'

# Each case: an Item added (a trailing \ makes a folder) with optional Text, Bytes, a release file it links From or a
# file it copies FromPath, or a release file Missing.
$Cases = [ordered]@{
    'fresh extraction'          = @{}
    'settings'                  = @{ Item = 'user\osl.ini' }
    'empty user folder'         = @{ Item = 'user\' }
    'save'                      = @{ Item = 'user\savedata\CUSA05083\SAVEDATA00\data' }
    'trophies'                  = @{ Item = 'user\trophy\CUSA05083\progress_user.txt' }
    'trophy key'                = @{ Item = 'keys.json' }
    'shader cache'              = @{ Item = 'cache\CUSA05083\pipeline' }
    'log'                       = @{ Item = 'logs\odin_sphere_leifthrasir_001.log' }
    'game files'                = @{ Item = 'game\prepared\CUSA05083-1.00\eboot.bin' }
    'install leftover'          = @{ Item = 'CUSA05083-1.00.install-backup\' }
    'game package'              = @{ Item = 'UP0177-CUSA05083.pkg' }
    'package by content'        = @{ Item = 'licenses\notes.txt'; Bytes = [byte[]](0x7F, 0x43, 0x4E, 0x54, 0, 0, 0, 0) }
    'loader pdb'                = @{ Item = 'odin_sphere_leifthrasir.pdb' }
    'updater pdb'               = @{ Item = 'trg-updater.pdb' }
    'debug file'                = @{ Item = 'trg-updater.debug' }
    'debug file in licenses'    = @{ Item = 'licenses\trg-updater.debug' }
    'updater workspace'         = @{ Item = 'trg-update-1234-5678\job.txt' }
    'updater staging'           = @{ Item = 'staging\odin_sphere_leifthrasir.exe'; From = 'odin_sphere_leifthrasir.exe' }
    'development helper'        = @{ Item = 'trg-updater.exe'; FromPath = (Join-Path $root 'build\cmake\clang64\bin\trg-updater.exe') }
    'development program'       = @{ Item = 'odin_sphere_leifthrasir.exe'; FromPath = (Join-Path $root 'build\cmake\clang64\bin\odin_sphere_leifthrasir.exe') }
    'backup file'               = @{ Item = 'licenses\THIRD-PARTY-LICENSES.txt.bak' }
    'save file'                 = @{ Item = 'licenses\slot.sav' }
    'renderdoc'                 = @{ Item = 'renderdoc.dll' }
    'vulkan layer'              = @{ Item = 'VkLayer_khronos_validation.json' }
    'vulkan loader'             = @{ Item = 'vulkan-1.dll' }
    'program in licenses'       = @{ Item = 'licenses\tool.exe' }
    'DEBUG_SAULO marker'        = @{ Item = 'README.txt'; Text = 'DEBUG_SAULO' }
    'repository path'           = @{ Item = 'licenses\notes.txt'; Text = $root }
    'repository path UTF-16'    = @{ Item = 'licenses\notes.txt'; Bytes = [Text.Encoding]::Unicode.GetBytes("x$root") }
    'user profile path'         = @{ Item = 'licenses\notes.txt'; Text = $env:USERPROFILE }
    'users folder'              = @{ Item = 'licenses\notes.txt'; Text = 'C:\Users\someone\odin' }
    'users folder UTF-16'       = @{ Item = 'licenses\notes.txt'; Bytes = [Text.Encoding]::Unicode.GetBytes('C:\Users\someone') }
    'VisualStudio path'         = @{ Item = 'licenses\notes.txt'; Text = 'S:\VisualStudio\repos' }
    'Decomp path'               = @{ Item = 'licenses\notes.txt'; Text = 'S:\Decomp\odin' }
    'crash dump'                = @{ Item = 'odin_sphere_leifthrasir.dmp' }
    'hang report'               = @{ Item = 'licenses\hang_001.txt' }
    'temporary file'            = @{ Item = 'download.tmp' }
    'python cache'              = @{ Item = '__pycache__\import_inventory.cpython-314.pyc' }
    'build folder'              = @{ Item = 'CMakeFiles\' }
    'object file'               = @{ Item = 'loader.obj' }
    'static library'            = @{ Item = 'libosl_launcher.a' }
    'source file'               = @{ Item = 'loader.c' }
    'test program'              = @{ Item = 'osl-tests.exe'; From = 'odin_sphere_leifthrasir.exe' }
    'missing program'           = @{ Missing = 'odin_sphere_leifthrasir.exe' }
    'program metadata'          = @{ Item = 'odin_sphere_leifthrasir.exe'; From = 'trg-updater.exe' }
    'obsolete program name'     = @{ Item = 'osl-loader.exe' }
    'obsolete name in a file'   = @{ Item = 'licenses\notes.txt'; Text = 'osl-loader.exe' }
    'missing update helper'     = @{ Missing = 'trg-updater.exe' }
    'helper not self-contained' = @{ Item = 'trg-updater.exe'; From = 'odin_sphere_leifthrasir.exe' }
    'missing DLL'               = @{ Missing = 'SDL3.dll' }
    'missing licences'          = @{ Missing = 'licenses\THIRD-PARTY-LICENSES.txt' }
    'missing header art'        = @{ Missing = 'assets\launcher\header-art.png' }
    'missing sponsor heart'     = @{ Missing = 'assets\launcher\sponsor-heart.png' }
}

function New-LinkedCopy([string]$Source, [string]$Target) {
    New-Item -ItemType Directory -Force $Target | Out-Null
    foreach ($item in Get-ChildItem -LiteralPath $Source -Recurse -Force) {
        $path = Join-Path $Target $item.FullName.Substring($Source.Length + 1)
        if ($item.PSIsContainer) { New-Item -ItemType Directory -Force $path | Out-Null }
        else { New-Item -ItemType HardLink -Path $path -Target $item.FullName | Out-Null }
    }
}

$failures = 0
$skipped = 0
$powershell = (Get-Process -Id $PID).Path
try {
    foreach ($case in $Cases.Keys) {
        $spec = $Cases[$case]
        if ($spec['FromPath'] -and -not (Test-Path -LiteralPath $spec['FromPath'] -PathType Leaf)) {
            Write-Warn "$case skipped: $($spec['FromPath']) does not exist"
            $skipped++
            continue
        }
        $stage = Join-Path $casesRoot ($case -replace '\W', '_')
        if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
        New-LinkedCopy $release $stage
        if ($spec['Missing']) { Remove-Item -LiteralPath (Join-Path $stage $spec['Missing']) -Force }
        if ($spec['Item']) {
            $target = Join-Path $stage $spec['Item']
            if ($spec['Item'].EndsWith('\')) {
                New-Item -ItemType Directory -Force $target | Out-Null
            } else {
                New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
                if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Force }
                if ($spec['From']) { New-Item -ItemType HardLink -Path $target -Target (Join-Path $release $spec['From']) | Out-Null }
                elseif ($spec['FromPath']) { Copy-Item -LiteralPath $spec['FromPath'] $target }
                elseif ($spec['Bytes']) { [IO.File]::WriteAllBytes($target, $spec['Bytes']) }
                elseif ($spec['Text']) { [IO.File]::WriteAllText($target, $spec['Text']) }
                else { [IO.File]::WriteAllText($target, 'placeholder') }
            }
        }
        & $powershell -NoProfile -File $checker -VerifyOnly $stage *> $null
        $accepted = $LASTEXITCODE -eq 0
        $expected = $spec.Count -eq 0
        if ($accepted -ne $expected) {
            Write-Bad "$case (accepted: $accepted)"
            $failures++
        }
        Remove-Item -LiteralPath $stage -Recurse -Force
    }
} finally {
    Remove-Item -LiteralPath $casesRoot -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "$($Cases.Count - $skipped) release check cases, $failures failures, $skipped skipped"
if ($failures) { exit 1 }
