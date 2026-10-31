<#
.SYNOPSIS
  Packages the tested build as dist\OdinSphereLeifthrasirPC-v<version>-windows-x64.zip and its .sha256 file. Local only:
  nothing is published, tagged or uploaded.
.DESCRIPTION
  Builds CMake's Release configuration with tools\build.ps1 -Release (build\cmake\clang64-release) and the release
  update helper with tools\build-updater.ps1 (MSYS2 CLANG64, static, debug information split off) unless -SkipBuild.
  It then stages odin_sphere_leifthrasir.exe without debug information, that trg-updater.exe, the MSYS2 DLLs the
  loader uses (found by walking its imports), the launcher header art, README.txt, LICENSE.txt and the licence texts.
  The staging folder is checked for game data, user files, logs, symbols, updater leftovers, debugging tools, local
  paths (ASCII and UTF-16), stale names and anything unexpected; odin_sphere_leifthrasir.exe must carry its product
  name and version, and trg-updater.exe must import nothing but Windows' own DLLs and hold no debug sections.
  The ZIP holds one top-level folder named after it, the layout the updater installs. It is opened again, its entry
  names checked, extracted and compared file by file with the staging folder, checked again, passed through the
  updater's own rules (osl-update-tests --check-package), and the update helper tests run against the packaged
  trg-updater.exe. trg-updater.debug goes to dist\<name>-symbols\, which is not packaged.
  The version is OSL_PORT_VERSION in CMakeLists.txt, the only place it is written.
.PARAMETER SkipBuild
  Packages the existing build\cmake\clang64-release\bin\odin_sphere_leifthrasir.exe and the existing release helper.
.PARAMETER Clean
  Passes -Clean to tools\build.ps1 -Release.
.PARAMETER VerifyOnly
  Only checks an existing staged folder or release ZIP, then exits.
.EXAMPLE
  .\tools\package.ps1
  .\tools\package.ps1 -VerifyOnly dist\OdinSphereLeifthrasirPC-v0.2.0-windows-x64.zip
#>
[CmdletBinding()]
param(
    [switch]$SkipBuild,
    [switch]$Clean,
    [string]$VerifyOnly
)

. (Join-Path $PSScriptRoot '_common.ps1')

$root = $script:RepoRoot
$buildDir = Join-Path $root 'build\cmake\clang64-release'
$loader = Join-Path $buildDir 'bin\odin_sphere_leifthrasir.exe'
$updaterBuild = Join-Path $buildDir 'trg-updater-clang64'
$updater = Join-Path $updaterBuild 'trg-updater.exe'
$updaterDebug = Join-Path $updaterBuild 'trg-updater.debug'
$updateTests = Join-Path $buildDir 'bin\osl-update-tests.exe'
$helperTests = Join-Path $buildDir 'bin\osl-update-helper-tests.exe'
$tc = Resolve-Toolchain 'auto'
$bin = $tc.Bin
$prefix = Split-Path $bin -Parent
$msys = Split-Path $prefix -Parent
$pacman = Join-Path $msys 'usr\bin\pacman.exe'
$pacmanDb = Join-Path $msys 'var\lib\pacman\local'
$objcopy = Join-Path $bin 'llvm-objcopy.exe'
$objdump = Join-Path $bin 'llvm-objdump.exe'
$packagePrefix = 'mingw-w64-clang-x86_64-'

# Licence texts of the code compiled into odin_sphere_leifthrasir.exe from this repository and its fetched dependencies.
$BuiltInLicenses = [ordered]@{
    'shadPS4'       = 'gpu\shadps4\LICENSE'
    'sirit'         = 'gpu\third_party\sirit\LICENSE.txt'
    'SPIRV-Headers' = 'gpu\third_party\sirit\externals\SPIRV-Headers\LICENSE'
    'half'          = 'gpu\third_party\half\LICENSE.txt'
    'SMAA'          = 'gpu\shadps4\video_core\host_shaders\smaa\LICENSE.txt'
    'DejaVu'        = 'gpu\third_party\fonts\LICENSE-DejaVu.txt'
    'NotoSans'      = 'third_party\fonts\LICENSE-NotoSans.txt'
    'NotoSansTC'    = 'third_party\fonts\LICENSE-NotoSansTC.txt'
    'imgui'         = 'gpu\third_party\imgui\LICENSE.txt'
    'magic_enum'    = 'gpu\third_party\magic_enum\LICENSE'
    'miniz'         = 'gpu\third_party\miniz\LICENSE'
    'xbyak'         = 'gpu\third_party\xbyak\COPYRIGHT'
}
# MSYS2 packages compiled in as headers or static libraries (libunwind: in trg-updater.exe; crt: in both programs).
$StaticPackages = @('fmt', 'boost', 'robin-map', 'vulkan-memory-allocator', 'vulkan-headers', 'compiler-rt', 'libunwind', 'crt')
# The packages whose DLLs may ship, each listed in packaging\licenses\THIRD-PARTY-LICENSES.txt. A DLL from any other
# package stops the packaging until its licence has been reviewed and added there.
$DllPackages = @('sdl3', 'freetype', 'libpng', 'zydis', 'xxhash', 'libc++', 'libwinpthread', 'zlib', 'bzip2',
    'harfbuzz', 'brotli', 'graphite2', 'pcre2', 'glib2', 'gettext-runtime', 'libiconv')
# Provided by the graphics driver.
$SystemProvided = @('vulkan-1.dll')
# trg-updater.exe runs alone from a temporary folder: it may load nothing but Windows itself.
$HelperSystemDlls = @('kernel32.dll', 'shell32.dll', 'user32.dll', 'advapi32.dll', 'ntdll.dll', 'shlwapi.dll',
    'ole32.dll', 'bcrypt.dll')

$RootFiles = @('odin_sphere_leifthrasir.exe', 'trg-updater.exe', 'README.txt', 'LICENSE.txt')
$AssetFiles = @('assets\launcher\header-art.png', 'assets\launcher\sponsor-heart.png')
$RequiredLicenses = @('licenses\THIRD-PARTY-LICENSES.txt', 'licenses\MSYS2-PACKAGES.txt', 'licenses\miniz\LICENSE')
# Game data, the player's files, logs, caches, symbols, updater leftovers and debugging tools.
$ForbiddenNames = @('game', 'user', 'logs', 'cache', 'build', 'prepared', 'extracted', 'image', 'savedata', 'trophy',
    'download0', 'temp0', 'shader', 'screenshots', 'sce_sys', 'sce_module', 'eboot.bin', 'param.sfo', 'osl.ini',
    'keys.json', 'boot.bin', 'image.json', 'imports.json', 'symbols', 'vulkan-1.dll', '__pycache__', 'CMakeFiles',
    'staging', 'backup', 'manifest.txt', 'job.txt')
$ForbiddenEverywhere = @('*.pkg', '*.pdb', '*.debug', '*.log', '*.dmp', 'hang_*.txt', 'trg-update-*', 'VkLayer_*', 'renderdoc*',
    '*.install-temp', '*.install-backup', '*.install-update', '*.pyc', '*.bak', '*.tmp', '*.sav', '*.part', '*.ilk')
# Licence texts have names such as COPYING.LIB, so these apply outside licenses\ (still checked for content below).
$ForbiddenPatterns = @('*.ini', '*.bin', '*.self', '*.sprx', '*.prx', '*.elf', '*.cpk', '*.sfo', '*.trp', '*.json',
    '*.spv', '*.pipeline_cache', '*.obj', '*.o', '*.a', '*.lib', '*.py', '*.ps1', '*.c', '*.cpp', '*.h', '*.hpp')
# odin_sphere_leifthrasir.exe's version resource (src\odin_sphere_leifthrasir.rc).
$ProgramMetadata = [ordered]@{
    ProductName = 'Odin Sphere Leifthrasir PC'; FileDescription = 'Odin Sphere Leifthrasir PC'
    OriginalFilename = 'odin_sphere_leifthrasir.exe'; InternalName = 'odin_sphere_leifthrasir'
}
# File signatures of PS4 packages, SELF/ELF executables and CRI archives.
$ForbiddenMagic = @{ '7F434E54' = 'PS4 package'; '4F153D1D' = 'PS4 SELF'; '7F454C46' = 'ELF executable'; '43504B20' = 'CPK archive' }

$Latin1 = [Text.Encoding]::GetEncoding(28591)
$LocalText = New-Object System.Collections.Generic.List[string]
foreach ($path in @($root, $env:USERPROFILE)) {
    if ($path) { $LocalText.Add($path); $LocalText.Add($path.Replace('\', '/')) }
}
foreach ($word in @('VisualStudio', 'S:\Decomp', 'S:/Decomp', 'C:\Users\', 'C:/Users/', 'DEBUG_SAULO', 'referencias-ps4',
        'build\cmake', 'build/cmake', 'dragons_crown', 'DragonsCrown', 'osl-loader', 'osl.loader')) {
    $LocalText.Add($word)
}
# Each needle as ASCII/UTF-8 and as UTF-16LE, both read through Latin-1 so one search covers either.
$LocalNeedles = foreach ($text in $LocalText) { $text; $Latin1.GetString([Text.Encoding]::Unicode.GetBytes($text)) }

function Get-Imports([string]$Path) {
    $out = & $objdump -p $Path
    if ($LASTEXITCODE -ne 0) { Stop-Tool "llvm-objdump failed on $Path" }
    foreach ($line in $out) { if ($line -match 'DLL Name:\s*(\S+)') { $Matches[1] } }
}

function Get-Sections([string]$Path) {
    $out = & $objdump -h $Path
    if ($LASTEXITCODE -ne 0) { Stop-Tool "llvm-objdump failed on $Path" }
    foreach ($line in $out) { if ($line -match '^\s*\d+\s+(\S+)') { $Matches[1] } }
}

# The DLLs that $Exe loads from $Dir, directly or through each other; Windows' own DLLs are not in $Dir.
function Get-DllClosure([string]$Exe, [string]$Dir) {
    $found = [ordered]@{}
    $queue = New-Object System.Collections.Generic.Queue[string]
    $queue.Enqueue($Exe)
    while ($queue.Count -gt 0) {
        foreach ($name in Get-Imports $queue.Dequeue()) {
            $key = $name.ToLowerInvariant()
            if ($SystemProvided -contains $key -or $found.Contains($key)) { continue }
            $file = Join-Path $Dir $name
            if (Test-Path -LiteralPath $file -PathType Leaf) {
                $found[$key] = (Get-Item -LiteralPath $file).FullName
                $queue.Enqueue($found[$key])
            }
        }
    }
    return $found
}

function Get-PackageInfo([string]$Short) {
    $name = $packagePrefix + $Short
    $query = & $pacman -Q $name 2>$null
    if ($LASTEXITCODE -ne 0) { Stop-Tool "MSYS2 package $name is not installed" }
    $version = ($query -split ' ')[1]
    $fields = @{}
    $key = $null
    foreach ($line in Get-Content -LiteralPath (Join-Path $pacmanDb "$name-$version\desc")) {
        if ($line -match '^%(\w+)%$') { $key = $Matches[1]; $fields[$key] = @() }
        elseif ($line -and $key) { $fields[$key] += $line }
    }
    $base = if ($fields.ContainsKey('BASE')) { $fields['BASE'][0] } else { $name }
    $licenses = @(& $pacman -Ql $name | ForEach-Object { ($_ -split ' ', 2)[1] } |
        Where-Object { $_ -match '/share/licenses/.+[^/]$' })
    return @{
        Short = $Short; Version = $version; License = ($fields['LICENSE'] -join ' ') -replace 'spdx:', ''
        Source = "https://mirror.msys2.org/mingw/sources/$base-$version.src.tar.zst"; Licenses = $licenses
    }
}

# Why trg-updater.exe is not the self-contained tools\build-updater.ps1 helper; empty when it is.
function Get-HelperProblems([string]$Helper) {
    $problems = New-Object System.Collections.Generic.List[string]
    $imports = @(Get-Imports $Helper)
    if ($imports.Count -eq 0) { $problems.Add('trg-updater.exe: no imports could be read') }
    foreach ($name in $imports) {
        if ($HelperSystemDlls -notcontains $name.ToLowerInvariant() -and $name -notmatch '^api-ms-win-') {
            $problems.Add("trg-updater.exe imports $name, which is not part of Windows")
        }
    }
    $sections = @(Get-Sections $Helper)
    if ($sections -match '^\.z?debug_') { $problems.Add('trg-updater.exe holds debug sections') }
    if ($sections -notcontains '.gnu_debuglink') { $problems.Add('trg-updater.exe is not the tools\build-updater.ps1 build (no .gnu_debuglink)') }
    return $problems
}

function Test-ReleaseFolder([string]$Dir) {
    $problems = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Dir -PathType Container)) { Stop-Tool "Not a folder: $Dir" }
    $base = (Resolve-Path -LiteralPath $Dir).Path.TrimEnd('\')
    $exe = Join-Path $base 'odin_sphere_leifthrasir.exe'
    $helper = Join-Path $base 'trg-updater.exe'
    $dlls = if (Test-Path -LiteralPath $exe) { Get-DllClosure $exe $base } else { [ordered]@{} }
    if (Test-Path -LiteralPath $exe) {
        foreach ($name in (Get-DllClosure $exe $bin).Keys) {
            if (-not $dlls.Contains($name)) { $problems.Add("missing DLL: $name") }
        }
    }
    if (Test-Path -LiteralPath $helper) {
        foreach ($problem in (Get-HelperProblems $helper)) { $problems.Add($problem) }
    }
    if (Test-Path -LiteralPath $exe) {
        $info = (Get-Item -LiteralPath $exe).VersionInfo
        foreach ($field in $ProgramMetadata.Keys) {
            if ($info.$field -ne $ProgramMetadata[$field]) { $problems.Add("odin_sphere_leifthrasir.exe ${field}: '$($info.$field)'") }
        }
        if ($info.ProductVersion -ne $version) {
            $problems.Add("odin_sphere_leifthrasir.exe is version '$($info.ProductVersion)', CMakeLists.txt says $version")
        }
        $header = New-Object byte[] 4096
        $stream = [IO.File]::OpenRead($exe)
        try { [void]$stream.Read($header, 0, $header.Length) } finally { $stream.Dispose() }
        if ([BitConverter]::ToUInt32($header, [BitConverter]::ToInt32($header, 0x3C) + 12) -ne 0) {
            $problems.Add('odin_sphere_leifthrasir.exe carries a COFF symbol table (not the Release link)')
        }
        if ((Get-Sections $exe) -contains '.buildid') { $problems.Add('odin_sphere_leifthrasir.exe carries a build id (not the Release link)') }
    }
    foreach ($item in Get-ChildItem -LiteralPath $base -Recurse -Force) {
        $relative = $item.FullName.Substring($base.Length + 1)
        $parts = $relative -split '\\'
        if ($ForbiddenNames -contains $item.Name) { $problems.Add("forbidden name: $relative") }
        $patterns = if ($parts[0] -eq 'licenses') { $ForbiddenEverywhere } else { $ForbiddenEverywhere + $ForbiddenPatterns }
        foreach ($pattern in $patterns) {
            if ($item.Name -like $pattern) { $problems.Add("forbidden file type: $relative") }
        }
        if ($parts.Count -eq 1) {
            if ($item.PSIsContainer) {
                if ($item.Name -notin @('licenses', 'assets')) { $problems.Add("unexpected folder: $relative") }
            } elseif ($RootFiles -notcontains $item.Name -and -not $dlls.Contains($item.Name.ToLowerInvariant())) {
                $problems.Add("unexpected file: $relative")
            }
        } elseif ($parts[0] -eq 'assets') {
            if ($item.PSIsContainer) {
                if ($relative -ne 'assets\launcher') { $problems.Add("unexpected folder: $relative") }
            } elseif ($AssetFiles -notcontains $relative) {
                $problems.Add("unexpected item: $relative")
            }
        } elseif ($parts[0] -ne 'licenses' -or $item.Name -like '*.exe' -or $item.Name -like '*.dll') {
            $problems.Add("unexpected item: $relative")
        }
        if ($item.PSIsContainer) { continue }
        $bytes = [IO.File]::ReadAllBytes($item.FullName)
        if ($bytes.Length -ge 4) {
            $magic = -join ($bytes[0..3] | ForEach-Object { $_.ToString('X2') })
            if ($ForbiddenMagic.ContainsKey($magic)) { $problems.Add("$($ForbiddenMagic[$magic]): $relative") }
        }
        $text = $Latin1.GetString($bytes)
        foreach ($needle in $LocalNeedles) {
            if ($text.IndexOf($needle, [StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $problems.Add("local path, development marker or stale name in $relative"); break
            }
        }
        if (($item.Name -like '*.exe' -or $item.Name -like '*.dll') -and $text.IndexOf('.debug_info', [StringComparison]::Ordinal) -ge 0) {
            $problems.Add("debug information in $relative")
        }
    }
    foreach ($leaf in $RootFiles + $AssetFiles + $RequiredLicenses) {
        if (-not (Test-Path -LiteralPath (Join-Path $base $leaf) -PathType Leaf)) { $problems.Add("missing: $leaf") }
    }
    return $problems
}

function Write-Problems($Problems) {
    Write-Host ''
    Write-Host 'RELEASE CHECK FAILED' -ForegroundColor Red
    foreach ($problem in $Problems) { Write-Host "  $problem" -ForegroundColor Red }
}

function Write-Text([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, ($Text -replace "\r?\n", "`r`n"), (New-Object Text.UTF8Encoding $false))
}

# Extracts a release ZIP into a new temporary folder, refusing absolute, parent or backslash entry names, folder
# entries and any entry outside the one top-level folder $Name.
function Expand-ReleaseZip([string]$Zip, [string]$Name) {
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $target = Join-Path ([IO.Path]::GetTempPath()) ('osl_release_zip_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory $target | Out-Null
    $archive = [IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        if ($archive.Entries.Count -eq 0) { Stop-Tool 'The ZIP is empty' }
        foreach ($entry in $archive.Entries) {
            if ($entry.FullName -match '^[\\/]|\.\.|:|\\|//') { Stop-Tool "The ZIP holds an unsafe entry name: $($entry.FullName)" }
            if (-not $entry.Name) { Stop-Tool "The ZIP holds a folder entry: $($entry.FullName)" }
            if (-not $entry.FullName.StartsWith("$Name/", [StringComparison]::Ordinal)) {
                Stop-Tool "The ZIP holds $($entry.FullName) outside its one folder $Name/"
            }
            $path = Join-Path $target $entry.FullName
            New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
            [IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $path)
        }
    } finally { $archive.Dispose() }
    $top = @(Get-ChildItem -LiteralPath $target -Force)
    if ($top.Count -ne 1 -or -not $top[0].PSIsContainer -or $top[0].Name -cne $Name) {
        Stop-Tool "The ZIP must hold exactly one top-level folder, $Name"
    }
    return $target
}

$cmake = Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmake -notmatch '(?m)^set\(OSL_PORT_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\)') { Stop-Tool 'OSL_PORT_VERSION not found in CMakeLists.txt' }
$version = $Matches[1]

if ($VerifyOnly) {
    $path = (Resolve-Path -LiteralPath $VerifyOnly).Path
    if (Test-Path -LiteralPath $path -PathType Leaf) {
        $zipName = [IO.Path]::GetFileNameWithoutExtension($path)
        $folder = Expand-ReleaseZip $path $zipName
        try { $problems = @(Test-ReleaseFolder (Join-Path $folder $zipName)) }
        finally { Remove-Item -LiteralPath $folder -Recurse -Force -ErrorAction SilentlyContinue }
    } else {
        $problems = @(Test-ReleaseFolder $path)
    }
    if ($problems.Count) { Write-Problems $problems; exit 1 }
    Write-Ok "Release folder is clean: $VerifyOnly"
    return
}

$name = "OdinSphereLeifthrasirPC-v$version-windows-x64"
$dist = Join-Path $root 'dist'
$stage = Join-Path $dist $name
$symbols = Join-Path $dist "$name-symbols"
$zip = Join-Path $dist "$name.zip"
$shaFile = Join-Path $dist "$name.sha256"

Write-Step "Odin Sphere Leifthrasir PC $version - Windows release package (local only)"
if (-not $SkipBuild) {
    $build = @{ Release = $true }
    if ($Clean) { $build.Clean = $true }
    & (Join-Path $PSScriptRoot 'build.ps1') @build
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'Build failed' }
    & (Join-Path $PSScriptRoot 'build-updater.ps1') -OutDir $updaterBuild
    if ($LASTEXITCODE -ne 0) { Stop-Tool 'Building the release update helper failed' }
}
foreach ($file in $loader, $updater, $updaterDebug) {
    if (-not (Test-Path -LiteralPath $file)) { Stop-Tool "Missing $file (run without -SkipBuild)" }
}
if (-not (Select-String -LiteralPath (Join-Path $buildDir 'CMakeCache.txt') -SimpleMatch 'CMAKE_BUILD_TYPE:STRING=Release' -Quiet)) {
    Stop-Tool "$buildDir is not the Release build of tools\build.ps1 -Release (run without -SkipBuild)"
}
$built = (Get-Item -LiteralPath $loader).VersionInfo.ProductVersion
if ($built -ne $version) { Stop-Tool "odin_sphere_leifthrasir.exe is version $built, CMakeLists.txt says $version (run without -SkipBuild)" }
$updaterTime = (Get-Item -LiteralPath $updater).LastWriteTimeUtc
$updaterInputs = @(Get-ChildItem -LiteralPath (Join-Path $root 'launcher\updater') -Recurse -File) +
    @(Get-Item -LiteralPath (Join-Path $root 'launcher\CMakeLists.txt'), (Join-Path $PSScriptRoot 'build-updater.ps1'))
$newer = @($updaterInputs | Where-Object { $_.LastWriteTimeUtc -gt $updaterTime })
if ($newer.Count) { Stop-Tool "$updater is older than $($newer[0].FullName) (run without -SkipBuild)" }

New-Item -ItemType Directory -Force $dist | Out-Null
foreach ($old in @($stage, $symbols, $zip, $shaFile)) {
    if (Test-Path -LiteralPath $old) { Remove-Item -LiteralPath $old -Recurse -Force }
}
$licenses = Join-Path $stage 'licenses'
New-Item -ItemType Directory $stage, $licenses, $symbols | Out-Null

Write-Step 'Staging odin_sphere_leifthrasir.exe without debug information'
& $objcopy --strip-debug $loader (Join-Path $stage 'odin_sphere_leifthrasir.exe')
if ($LASTEXITCODE -ne 0) { Stop-Tool 'llvm-objcopy --strip-debug failed on odin_sphere_leifthrasir.exe' }
Write-Step 'Staging the release trg-updater.exe; its trg-updater.debug goes to the symbols folder'
Copy-Item -LiteralPath $updater $stage
Copy-Item -LiteralPath $updaterDebug $symbols
$helperProblems = @(Get-HelperProblems (Join-Path $stage 'trg-updater.exe'))
if ($helperProblems.Count) { Write-Problems $helperProblems; Stop-Tool 'trg-updater.exe is not the self-contained release helper' }

Write-Step 'Staging the DLLs the loader uses'
$dlls = Get-DllClosure $loader $bin
$owners = [ordered]@{}
foreach ($dll in $dlls.Values) {
    $owner = & $pacman -Qqo $dll.Substring($msys.Length).Replace('\', '/') 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $owner) { Stop-Tool "No MSYS2 package owns $dll" }
    $short = $owner.Substring($packagePrefix.Length)
    if ($DllPackages -notcontains $short) {
        Stop-Tool "$(Split-Path $dll -Leaf) comes from $owner, which is not in packaging\licenses\THIRD-PARTY-LICENSES.txt"
    }
    if (-not $owners.Contains($short)) { $owners[$short] = @() }
    $owners[$short] += Split-Path $dll -Leaf
    Copy-Item -LiteralPath $dll $stage
}

Write-Step 'Staging the licences'
Copy-Item -LiteralPath (Join-Path $root 'LICENSE') (Join-Path $stage 'LICENSE.txt')
Copy-Item -LiteralPath (Join-Path $root 'packaging\licenses\THIRD-PARTY-LICENSES.txt') $licenses
foreach ($component in $BuiltInLicenses.Keys) {
    $source = Join-Path $root $BuiltInLicenses[$component]
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { Stop-Tool "Missing licence file: $source" }
    New-Item -ItemType Directory -Force (Join-Path $licenses $component) | Out-Null
    Copy-Item -LiteralPath $source (Join-Path $licenses $component)
}
$manifest = New-Object System.Collections.Generic.List[string]
$manifest.Add("MSYS2 CLANG64 packages in odin_sphere_leifthrasir.exe and trg-updater.exe $version and next to them. Source packages: the URL on each line.")
foreach ($short in @($owners.Keys) + $StaticPackages) {
    $info = Get-PackageInfo $short
    if ($info.Licenses.Count -eq 0) { Stop-Tool "MSYS2 package $short installs no licence file" }
    foreach ($file in $info.Licenses) {
        $relative = $file.Substring($file.IndexOf('/share/licenses/') + '/share/licenses/'.Length).Replace('/', '\')
        $target = Join-Path $licenses $relative
        New-Item -ItemType Directory -Force (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath (Join-Path $prefix ($file.Substring($file.IndexOf('/share/licenses/') + 1))) $target
    }
    $use = if ($owners.Contains($short)) { $owners[$short] -join ', ' } else { 'compiled in' }
    $manifest.Add('')
    $manifest.Add("$short $($info.Version) ($use)")
    $manifest.Add("  Licence: $($info.License)")
    $manifest.Add("  Source:  $($info.Source)")
}
Write-Text (Join-Path $licenses 'MSYS2-PACKAGES.txt') (($manifest -join "`n") + "`n")
$readme = (Get-Content -LiteralPath (Join-Path $root 'packaging\README.txt') -Raw).Replace('@VERSION@', $version)
Write-Text (Join-Path $stage 'README.txt') $readme

Write-Step 'Staging the launcher images'
$assetDir = Join-Path $stage 'assets\launcher'
New-Item -ItemType Directory -Force $assetDir | Out-Null
foreach ($image in @('header-art.png', 'sponsor-heart.png')) {
    $source = Join-Path $root "assets\launcher\$image"
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { Stop-Tool "Missing launcher image: $source" }
    Copy-Item -LiteralPath $source $assetDir
}

Write-Step 'Checking the release folder'
$problems = @(Test-ReleaseFolder $stage)
if ($problems.Count) { Write-Problems $problems; Stop-Tool 'The staging folder holds files that must not be released' }

Write-Step "Writing the ZIP (one top-level folder, $name)"
Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
$files = @(Get-ChildItem -LiteralPath $stage -Recurse -File |
    Sort-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') })
$entries = @($files | ForEach-Object { $_.FullName.Substring($stage.Length + 1).Replace('\', '/') })
$stream = [IO.File]::Open($zip, [IO.FileMode]::CreateNew)
try {
    $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create)
    try {
        for ($i = 0; $i -lt $files.Count; ++$i) {
            $entry = "$name/" + $entries[$i]
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $files[$i].FullName, $entry,
                [IO.Compression.CompressionLevel]::Optimal)
        }
    } finally { $archive.Dispose() }
} finally { $stream.Dispose() }

Write-Step 'Checking the ZIP: entry names, the extracted files against the staging folder, the release check'
$extracted = Expand-ReleaseZip $zip $name
try {
    $content = Join-Path $extracted $name
    $listed = @(Get-ChildItem -LiteralPath $content -Recurse -File |
        ForEach-Object { $_.FullName.Substring($content.Length + 1).Replace('\', '/') } | Sort-Object)
    if (Compare-Object ($entries | Sort-Object) $listed -CaseSensitive) { Stop-Tool "The ZIP does not hold exactly the staged files inside $name/" }
    foreach ($entry in $entries) {
        $relative = $entry.Replace('/', '\')
        $staged = (Get-FileHash -LiteralPath (Join-Path $stage $relative) -Algorithm SHA256).Hash
        if ($staged -ne (Get-FileHash -LiteralPath (Join-Path $content $relative) -Algorithm SHA256).Hash) {
            Stop-Tool "$entry in the ZIP differs from the staged file"
        }
    }
    $problems = @(Test-ReleaseFolder $content)
    if ($problems.Count) { Write-Problems $problems; Stop-Tool 'The ZIP holds files that must not be released' }
} finally { Remove-Item -LiteralPath $extracted -Recurse -Force -ErrorAction SilentlyContinue }

$env:PATH = "$bin;$env:PATH"
Write-Step 'Checking the ZIP with the updater''s own rules'
& ninja -C $buildDir osl-update-tests osl-update-helper-tests
if ($LASTEXITCODE -ne 0) { Stop-Tool 'Building the updater tests failed' }
& $updateTests --check-package $zip
if ($LASTEXITCODE -ne 0) { Stop-Tool 'An installed release would refuse this ZIP as an update' }
Write-Step 'Running the update helper tests against the packaged trg-updater.exe'
& $helperTests (Join-Path $stage 'trg-updater.exe')
if ($LASTEXITCODE -ne 0) { Stop-Tool 'The packaged trg-updater.exe failed the update helper tests' }

$hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
[IO.File]::WriteAllText($shaFile, "$hash  $name.zip`n", (New-Object Text.UTF8Encoding $false))

function Format-Size([long]$Bytes) { '{0:N1} MB' -f ($Bytes / 1MB) }
$dllBytes = ($dlls.Values | ForEach-Object { (Get-Item -LiteralPath $_).Length } | Measure-Object -Sum).Sum
$stageBytes = ($files | Measure-Object -Property Length -Sum).Sum
Write-Host ''
Write-Host 'RELEASE PACKAGE READY (nothing was published)' -ForegroundColor Green
Write-Host "  Folder:   $stage ($(Format-Size $stageBytes), $($files.Count) files)"
Write-Host "  ZIP:      $zip ($(Format-Size (Get-Item -LiteralPath $zip).Length), one folder: $name/)"
Write-Host "  SHA-256:  $hash"
Write-Host "  Checksum: $shaFile"
Write-Host "  Symbols:  $symbols (not in the package)"
Write-Host "  Loader:   $(Format-Size (Get-Item -LiteralPath $loader).Length) built, $(Format-Size (Get-Item -LiteralPath (Join-Path $stage 'odin_sphere_leifthrasir.exe')).Length) packaged"
Write-Host "  DLLs:     $($dlls.Count), $(Format-Size $dllBytes)"
Write-Host "  Contents (inside $name/):"
foreach ($entry in $entries) { Write-Host "    $entry" }
