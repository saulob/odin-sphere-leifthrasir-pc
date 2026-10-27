<#
.SYNOPSIS
  Checks that tools\package.ps1 -VerifyOnly accepts the staged release folder and refuses it once it holds the
  player's files, game data, logs, symbols, updater leftovers, debugging tools, development markers or local paths, or
  lacks the update helper, a DLL, the licences or the header art. Nothing is built, launched or published.
.DESCRIPTION
  Needs the staged folder of tools\package.ps1 (dist\OdinSphereLeifthrasir-v<version>-windows-x64). Each case is a
  copy made of hard links in build\release-check-tests; a file a case changes is replaced, never written through
  its link, so the staged folder stays as it was.
#>
[CmdletBinding()]
param()

. (Join-Path $PSScriptRoot '_common.ps1')

$root = $script:RepoRoot
$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^set\(OSL_PORT_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)') { Stop-Tool 'OSL_PORT_VERSION not found in CMakeLists.txt' }
$release = Join-Path $root "dist\OdinSphereLeifthrasir-v$($Matches[1])-windows-x64"
if (-not (Test-Path -LiteralPath (Join-Path $release 'odin_sphere_leifthrasir.exe'))) { Stop-Tool "Run tools\package.ps1 first: $release is missing" }
$checker = Join-Path $PSScriptRoot 'package.ps1'
$casesRoot = Join-Path $root 'build\release-check-tests'

# Each case: an Item added (a trailing \ makes a folder) with optional Text, Bytes or a release file it links From, or
# a release file Missing.
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
    'debug file'                = @{ Item = 'trg-updater.debug' }
    'updater workspace'         = @{ Item = 'trg-update-1234-5678\job.txt' }
    'renderdoc'                 = @{ Item = 'renderdoc.dll' }
    'vulkan layer'              = @{ Item = 'VkLayer_khronos_validation.json' }
    'vulkan loader'             = @{ Item = 'vulkan-1.dll' }
    'program in licenses'       = @{ Item = 'licenses\tool.exe' }
    'DEBUG_SAULO marker'        = @{ Item = 'README.txt'; Text = 'DEBUG_SAULO' }
    'repository path'           = @{ Item = 'licenses\notes.txt'; Text = $root }
    'user profile path'         = @{ Item = 'licenses\notes.txt'; Text = $env:USERPROFILE }
    'missing program'           = @{ Missing = 'odin_sphere_leifthrasir.exe' }
    'obsolete program name'     = @{ Item = 'osl-loader.exe' }
    'missing update helper'     = @{ Missing = 'trg-updater.exe' }
    'helper not self-contained' = @{ Item = 'trg-updater.exe'; From = 'odin_sphere_leifthrasir.exe' }
    'missing DLL'               = @{ Missing = 'SDL3.dll' }
    'missing licences'          = @{ Missing = 'licenses\THIRD-PARTY-LICENSES.txt' }
    'missing header art'        = @{ Missing = 'assets\launcher\header-art.png' }
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
$powershell = (Get-Process -Id $PID).Path
try {
    foreach ($case in $Cases.Keys) {
        $spec = $Cases[$case]
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
                elseif ($spec['Bytes']) { [IO.File]::WriteAllBytes($target, $spec['Bytes']) }
                elseif ($spec['Text']) { [IO.File]::WriteAllText($target, $spec['Text']) }
                else { [IO.File]::WriteAllText($target, 'placeholder') }
            }
        }
        & $powershell -NoProfile -ExecutionPolicy Bypass -File $checker -VerifyOnly $stage *> $null
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
Write-Host "$($Cases.Count) release check cases, $failures failures"
if ($failures) { exit 1 }
