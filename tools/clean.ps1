<#
.SYNOPSIS
  Removes generated build output. Never touches the .pkg in game\, game\extracted or saves.
.PARAMETER Prepared
  Also removes the layered game views in game\prepared (hard-link names only; the extracted data stays).
.PARAMETER Logs
  Also removes user\*.log.
#>
[CmdletBinding(SupportsShouldProcess = $true)]
param(
    [switch]$Prepared,
    [switch]$Logs
)

. (Join-Path $PSScriptRoot '_common.ps1')

function Remove-Generated([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $full = (Resolve-Path -LiteralPath $Path).Path
    if (-not $full.StartsWith($script:RepoRoot + '\')) { Stop-Tool "refusing to delete outside the repository: $full" }
    if ($PSCmdlet.ShouldProcess($full, 'Remove')) {
        Remove-Item -LiteralPath $full -Recurse -Force
        Write-Ok "removed $full"
    }
}

Write-Step 'Cleaning build output'
Remove-Generated (Join-Path $script:RepoRoot 'build')
Remove-Generated (Join-Path $script:RepoRoot 'dist')

if ($Prepared) {
    Write-Step 'Cleaning layered game views'
    $preparedDir = Join-Path $script:RepoRoot 'game\prepared'
    if (Test-Path -LiteralPath $preparedDir) {
        foreach ($view in Get-ChildItem -LiteralPath $preparedDir -Directory) {
            if (-not (Test-Path -LiteralPath (Join-Path $view.FullName '.osl_view.json'))) {
                Write-Warn "skipping $($view.FullName): not a prepared game folder"
                continue
            }
            Remove-Generated $view.FullName
        }
    }
}
if ($Logs) {
    Write-Step 'Cleaning logs'
    $userDir = Join-Path $script:RepoRoot 'user'
    if (Test-Path -LiteralPath $userDir) {
        foreach ($log in @(Get-ChildItem -LiteralPath $userDir -Filter '*.log')) { Remove-Generated $log.FullName }
    }
}
