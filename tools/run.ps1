<# Compatibility wrapper: the same as tools\build.ps1 -Run. #>
& (Join-Path $PSScriptRoot 'build.ps1') -Run @args
exit $LASTEXITCODE
