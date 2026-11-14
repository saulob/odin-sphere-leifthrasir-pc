# Deploys the Bug Report Worker (server\odin-bug-reports) with Wrangler. Wrangler's own configuration decides where it goes.
#   .\tools\worker.ps1            npx wrangler deploy
#   .\tools\worker.ps1 -DryRun    npx wrangler deploy --dry-run (bundles, uploads nothing)
# The admin token is never set here: run `npx wrangler secret put OSL_BUG_REPORT_ADMIN_TOKEN` in server\odin-bug-reports yourself.
param([switch]$DryRun)

$ErrorActionPreference = 'Stop'

function Fail([string]$message) {
    [Console]::Error.WriteLine("worker: $message")
    exit 1
}

$repoRoot = Split-Path -Parent $PSScriptRoot
$workerDir = Join-Path $repoRoot 'server\odin-bug-reports'

if (-not (Test-Path -LiteralPath $workerDir -PathType Container)) { Fail "missing folder $workerDir" }
$configFile = Join-Path $workerDir 'wrangler.toml'
if (-not (Test-Path -LiteralPath $configFile -PathType Leaf)) { Fail "missing $configFile, the Wrangler configuration" }
$packageFile = Join-Path $workerDir 'package.json'
if (-not (Test-Path -LiteralPath $packageFile -PathType Leaf)) { Fail "missing $packageFile" }

$config = (Get-Content -Raw -LiteralPath $configFile) -split "`r?`n" | Where-Object { $_.TrimStart() -notmatch '^#' }
$configText = $config -join "`n"
if ($configText -notmatch '(?m)^\s*main\s*=\s*"([^"]+)"') { Fail "$configFile names no main entry" }
$entry = Join-Path $workerDir $Matches[1]
if (-not (Test-Path -LiteralPath $entry -PathType Leaf)) { Fail "missing $entry, the entry point named by wrangler.toml" }

# Every module the entry point imports, directly or not.
$pending = [System.Collections.Generic.Queue[string]]::new()
$seen = @{}
$pending.Enqueue((Resolve-Path -LiteralPath $entry).Path)
while ($pending.Count -gt 0) {
    $file = $pending.Dequeue()
    if ($seen[$file]) { continue }
    $seen[$file] = $true
    foreach ($import in [regex]::Matches((Get-Content -Raw -LiteralPath $file), "from\s+'(\.[^']+)'")) {
        $module = [System.IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $file) $import.Groups[1].Value))
        if (-not (Test-Path -LiteralPath $module -PathType Leaf)) { Fail "missing $module, imported by $file" }
        $pending.Enqueue($module)
    }
}

$namespaces = [regex]::Matches($configText, '(?m)^\s*namespace_id\s*=\s*"([^"]*)"')
if ($namespaces.Count -eq 0) { Fail "$configFile has no rate-limit namespace_id; the REPORT_LIMITER binding is required" }
foreach ($namespace in $namespaces) {
    if ($namespace.Groups[1].Value -notmatch '^[1-9][0-9]{2,8}$') {
        Fail "namespace_id is still '$($namespace.Groups[1].Value)' in $configFile. Set an integer that no other rate-limit binding in your Cloudflare account uses."
    }
}
if ($configText -match '(?im)^\s*[\w.\-]+\s*=\s*"[^"]*(REPLACE|SET_UNIQUE|TODO|CHANGEME|PLACEHOLDER|YOUR_)[^"]*"') {
    Fail "$configFile still holds a placeholder: $($Matches[0].Trim())"
}

if (-not (Get-Command npx -ErrorAction SilentlyContinue)) { Fail 'npx not found; install Node.js (https://nodejs.org) and open a new terminal' }

$wrangler = @('wrangler', 'deploy')
if ($DryRun) { $wrangler += '--dry-run' }

Push-Location -LiteralPath $workerDir
try {
    & npx @wrangler
    $code = $LASTEXITCODE
} finally {
    Pop-Location
}

if ($code -ne 0) {
    [Console]::Error.WriteLine("worker: wrangler exited with code $code")
    exit $code
}
if ($DryRun) {
    Write-Host 'Worker bundle checked (dry run, nothing uploaded).'
} else {
    Write-Host 'Worker deployed.'
    Write-Host 'Next: check /health, set OSL_BUG_REPORT_ADMIN_TOKEN if it is not set yet, open /admin.'
}
