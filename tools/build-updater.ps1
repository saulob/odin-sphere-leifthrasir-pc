<#
.SYNOPSIS
  Builds the release update helper, trg-updater.exe, with MSYS2 CLANG64. Local only.
.DESCRIPTION
  Compiles the helper sources listed in launcher\CMakeLists.txt (OSL_UPDATER_HELPER_SOURCES) with the MSYS2 CLANG64
  clang++ and LLD into a static libc++ helper that uses only Windows system DLLs, including the Windows UCRT: the model
  of the Dragon's Crown Pro PC, Super Street Fighter II Turbo HD Remix and Crackdown update helpers. The debug
  information is split with llvm-objcopy into trg-updater.debug next to it; the executable keeps only a .gnu_debuglink
  to it. tools\package.ps1 ships this build; the CMake trg-updater target stays for development and the tests.
.PARAMETER OutDir
  The output folder (default: build\cmake\clang64-release\trg-updater-clang64). It is emptied first.
.EXAMPLE
  .\tools\build-updater.ps1
#>
[CmdletBinding()]
param(
    [string]$OutDir
)

. (Join-Path $PSScriptRoot '_common.ps1')

$root = $script:RepoRoot
if (-not $OutDir) { $OutDir = Join-Path $root 'build\cmake\clang64-release\trg-updater-clang64' }
elseif (-not [IO.Path]::IsPathRooted($OutDir)) { $OutDir = Join-Path $root $OutDir }
$tc = Resolve-Toolchain 'auto'
$cxx = Join-Path $tc.Bin 'clang++.exe'
$objcopy = Join-Path $tc.Bin 'llvm-objcopy.exe'
$objdump = Join-Path $tc.Bin 'llvm-objdump.exe'
foreach ($tool in $cxx, $objcopy, $objdump, (Join-Path $tc.Bin 'ld.lld.exe')) {
    if (-not (Test-Path -LiteralPath $tool)) { Stop-Tool "Missing $tool (pacman -S mingw-w64-clang-x86_64-toolchain)" }
}

$cmake = Get-Content -LiteralPath (Join-Path $root 'launcher\CMakeLists.txt') -Raw
if ($cmake -notmatch '(?s)set\(OSL_UPDATER_HELPER_SOURCES\s+([^)]*)\)') { Stop-Tool 'OSL_UPDATER_HELPER_SOURCES not found in launcher\CMakeLists.txt' }
$sources = @($Matches[1] -split '\s+' | Where-Object { $_ } | ForEach-Object { "launcher/$_" })
$rootForward = $root.Replace('\', '/')
$compileFlags = @('-O2', '-g', '-DNDEBUG', '-std=c++20', "-fmacro-prefix-map=$rootForward/=", '-Wall', '-Wextra',
    '-DOSL_UPDATER_HELPER_BUILD=1', '-I', "$rootForward/launcher")
$linkFlags = @('-O2', '-g', '-DNDEBUG', '-mwindows', '-static', '-fuse-ld=lld')
$libraries = @('-lshell32', '-lkernel32', '-luser32', '-lgdi32', '-lwinspool', '-lshell32', '-lole32', '-loleaut32', '-luuid',
    '-lcomdlg32', '-ladvapi32')
# The helper is copied to a temporary folder and run from there, so it may load nothing but Windows itself.
$systemDlls = @('kernel32.dll', 'shell32.dll', 'user32.dll', 'advapi32.dll', 'ntdll.dll', 'shlwapi.dll', 'ole32.dll', 'bcrypt.dll')

if (Test-Path -LiteralPath $OutDir) { Remove-Item -LiteralPath $OutDir -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $OutDir 'obj') | Out-Null
$savedPath = $env:PATH
Push-Location $OutDir
try {
    $env:PATH = "$($tc.Bin);$env:PATH"
    Write-Step "Update helper: $((& $cxx --version | Select-Object -First 1))"
    $objects = foreach ($source in $sources) {
        $object = 'obj/' + (Split-Path $source -Leaf) + '.obj'
        & $cxx @compileFlags -c "$rootForward/$source" -o $object
        if ($LASTEXITCODE -ne 0) { Stop-Tool "Compiling $source failed" }
        $object
    }
    & $cxx @linkFlags @objects -o obj/trg-updater.exe @libraries
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'Linking trg-updater.exe failed' }
    & $objcopy --only-keep-debug obj/trg-updater.exe trg-updater.debug
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'llvm-objcopy --only-keep-debug failed' }
    & $objcopy --strip-debug --add-gnu-debuglink=trg-updater.debug obj/trg-updater.exe trg-updater.exe
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'llvm-objcopy --strip-debug failed' }

    $imports = @(& $objdump -p trg-updater.exe | ForEach-Object { if ($_ -match 'DLL Name:\s*(\S+)') { $Matches[1] } })
    if ($LASTEXITCODE -ne 0 -or -not $imports.Count) { Stop-Tool 'llvm-objdump could not read the imports of trg-updater.exe' }
    $foreign = @($imports | Where-Object { $_.ToLowerInvariant() -notin $systemDlls -and $_ -notmatch '^api-ms-win-' })
    if ($foreign.Count) { Stop-Tool "trg-updater.exe imports non-system DLLs: $($foreign -join ', ')" }
    $sections = @(& $objdump -h trg-updater.exe | ForEach-Object { if ($_ -match '^\s*\d+\s+(\S+)') { $Matches[1] } })
    if ($sections -match '^\.z?debug_') { Stop-Tool 'trg-updater.exe still holds debug sections' }
    if ($sections -notcontains '.gnu_debuglink') { Stop-Tool 'trg-updater.exe has no .gnu_debuglink' }
} finally {
    $env:PATH = $savedPath
    Pop-Location
}
Write-Ok "$(Join-Path $OutDir 'trg-updater.exe') ($($imports.Count) system DLLs imported, no debug sections)"
Write-Ok "$(Join-Path $OutDir 'trg-updater.debug') (not packaged)"
