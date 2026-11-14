# Static and stand-in checks of tools\worker.ps1: a copy of the Worker folder in a temporary tree, and an npx that only
# records its arguments. Nothing reaches Cloudflare, Wrangler or the network.
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$shell = (Get-Process -Id $PID).Path
$temp = Join-Path ([System.IO.Path]::GetTempPath()) "osl-worker-script-tests-$PID"
$failures = 0
$checks = 0

function Check([bool]$condition, [string]$what) {
    $script:checks++
    if (-not $condition) { $script:failures++; Write-Host "FAIL $what" }
}

function New-Tree([string]$Name, [string]$NamespaceId) {
    $tree = Join-Path $temp $Name
    $null = New-Item -ItemType Directory -Force -Path (Join-Path $tree 'tools')
    Copy-Item -LiteralPath (Join-Path $root 'tools\worker.ps1') -Destination (Join-Path $tree 'tools')
    $worker = Join-Path $tree 'server\odin-bug-reports'
    $null = New-Item -ItemType Directory -Force -Path $worker
    foreach ($name in 'src', 'package.json', 'wrangler.toml') {
        Copy-Item -Recurse -LiteralPath (Join-Path $root "server\odin-bug-reports\$name") -Destination $worker
    }
    if ($NamespaceId) {
        $config = Join-Path $worker 'wrangler.toml'
        (Get-Content -Raw -LiteralPath $config) -replace '(?m)^namespace_id = ".*"$', "namespace_id = `"$NamespaceId`"" |
            Set-Content -NoNewline -LiteralPath $config
    }
    return $tree
}

function Invoke-Worker([string]$Tree, [string[]]$Arguments = @(), [int]$NpxCode = 0) {
    $log = Join-Path $Tree 'npx.log'
    Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
    $env:OSL_FAKE_NPX_LOG = $log
    $env:OSL_FAKE_NPX_CODE = "$NpxCode"
    $outFile = Join-Path $Tree 'out.txt'
    $errFile = Join-Path $Tree 'err.txt'
    $process = Start-Process -FilePath $shell -ArgumentList (@('-NoProfile', '-NonInteractive', '-File', (Join-Path $Tree 'tools\worker.ps1')) + $Arguments) `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    [pscustomobject]@{
        Code = $process.ExitCode
        Out = [System.IO.File]::ReadAllText($outFile)
        Err = [System.IO.File]::ReadAllText($errFile)
        Npx = if (Test-Path -LiteralPath $log) { [System.IO.File]::ReadAllText($log).Trim() } else { $null }
    }
}

try {
    $shim = Join-Path $temp 'bin'
    $null = New-Item -ItemType Directory -Force -Path $shim
    Set-Content -Encoding ASCII -LiteralPath (Join-Path $shim 'npx.cmd') -Value @('@echo off', '>>"%OSL_FAKE_NPX_LOG%" echo %*', 'exit /b %OSL_FAKE_NPX_CODE%')
    $env:PATH = "$shim;$env:PATH"

    $shipped = Invoke-Worker (New-Tree 'shipped' $null) @('-DryRun')
    $shippedConfig = Get-Content -Raw -LiteralPath (Join-Path $root 'server\odin-bug-reports\wrangler.toml')
    if ($shippedConfig -match '(?m)^namespace_id = "[1-9][0-9]{2,8}"$') {
        Check ($shipped.Code -eq 0 -and $shipped.Npx -match 'wrangler deploy --dry-run') 'a resolved namespace_id passes the checks'
    } else {
        Check ($shipped.Code -eq 1 -and $shipped.Err -match 'namespace_id' -and $null -eq $shipped.Npx) 'the placeholder namespace_id stops the deploy before Wrangler'
    }

    $placeholder = Invoke-Worker (New-Tree 'placeholder' 'SET_UNIQUE_ODIN_RATE_LIMIT_NAMESPACE_ID')
    Check ($placeholder.Code -eq 1 -and $placeholder.Err -match 'namespace_id' -and $null -eq $placeholder.Npx -and $placeholder.Out -notmatch 'deployed') 'placeholder: fails, Wrangler never runs'
    $placeholderDry = Invoke-Worker (New-Tree 'placeholder-dry' 'REPLACE_ME') @('-DryRun')
    Check ($placeholderDry.Code -eq 1 -and $null -eq $placeholderDry.Npx -and $placeholderDry.Out -notmatch 'dry run') 'placeholder: a dry run fails the same way'

    $dry = Invoke-Worker (New-Tree 'dry' '4711') @('-DryRun')
    Check ($dry.Code -eq 0 -and $dry.Npx -eq 'wrangler deploy --dry-run') 'dry run: only wrangler deploy --dry-run is invoked'
    Check ($dry.Out -match 'nothing uploaded' -and $dry.Out -notmatch 'Worker deployed') 'dry run: says nothing was uploaded and never says deployed'

    $real = Invoke-Worker (New-Tree 'real' '4711')
    Check ($real.Code -eq 0 -and $real.Npx -eq 'wrangler deploy') 'deploy: only wrangler deploy is invoked'
    Check ($real.Out -match 'Worker deployed' -and $real.Out -notmatch 'nothing uploaded') 'deploy: says the Worker was deployed'

    $broken = Invoke-Worker (New-Tree 'broken' '4711') @() 3
    Check ($broken.Code -eq 3 -and $broken.Out -notmatch 'Worker deployed' -and $broken.Err -match 'exited with code 3') 'a Wrangler failure is passed on and not reported as deployed'

    $missing = New-Tree 'missing-module' '4711'
    Remove-Item -LiteralPath (Join-Path $missing 'server\odin-bug-reports\src\limits.js')
    $lost = Invoke-Worker $missing
    Check ($lost.Code -eq 1 -and $lost.Err -match 'limits\.js' -and $null -eq $lost.Npx) 'a missing imported module stops the deploy before Wrangler'

    $noEntry = New-Tree 'no-entry' '4711'
    Remove-Item -LiteralPath (Join-Path $noEntry 'server\odin-bug-reports\src\worker.js')
    Check ((Invoke-Worker $noEntry).Code -eq 1) 'a missing entry point stops the deploy'

    $noConfig = New-Tree 'no-config' '4711'
    Remove-Item -LiteralPath (Join-Path $noConfig 'server\odin-bug-reports\wrangler.toml')
    Check ((Invoke-Worker $noConfig).Code -eq 1) 'a missing wrangler.toml stops the deploy'

    $noBinding = New-Tree 'no-binding' '4711'
    $config = Join-Path $noBinding 'server\odin-bug-reports\wrangler.toml'
    (Get-Content -Raw -LiteralPath $config) -replace '(?m)^namespace_id = ".*"$', '' | Set-Content -NoNewline -LiteralPath $config
    Check ((Invoke-Worker $noBinding).Code -eq 1) 'a configuration without the rate-limit binding stops the deploy'

    $lines = Get-Content -LiteralPath (Join-Path $root 'tools\worker.ps1') | Where-Object { $_.TrimStart() -notmatch '^#' }
    Check (-not ($lines -match 'secret\s+(put|bulk)|CLOUDFLARE_API_TOKEN|account_id|Bearer')) 'the script sets no secret and holds no credential'
    Check (-not ($lines -match 'wrangler\s+(delete|rollback|versions)|--force')) 'the script only deploys'
} finally {
    Remove-Item Env:\OSL_FAKE_NPX_LOG, Env:\OSL_FAKE_NPX_CODE -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force -LiteralPath $temp -ErrorAction SilentlyContinue
}

Write-Host "worker script: $checks checks, $failures failed"
if ($failures) { exit 1 }
exit 0
