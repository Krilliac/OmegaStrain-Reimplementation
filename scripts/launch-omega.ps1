[CmdletBinding()]
param(
    [switch]$Debugger,
    [switch]$Resume,
    [switch]$SlowBoot,
    [string]$GameArgs,
    [switch]$DryRun
)

Write-Warning 'scripts\launch-omega.ps1 is deprecated; use scripts\launch-pcsx2-reference.ps1.'

& (Join-Path $PSScriptRoot 'launch-pcsx2-reference.ps1') @PSBoundParameters
