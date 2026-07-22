[CmdletBinding()]
param(
    [Alias('Configuration')]
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'Debug',

    [ValidateSet('auto', 'msvc', 'vs2022-x64')]
    [string]$Preset = 'auto'
)

Set-StrictMode -Version 2.0

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$presetCandidates = if ($Preset -eq 'auto') {
    @('msvc', 'vs2022-x64')
}
else {
    @($Preset)
}

$candidateExecutables = foreach ($candidatePreset in $presetCandidates) {
    Join-Path $repoRoot (Join-Path 'build' (Join-Path $candidatePreset `
        (Join-Path 'products\game' (Join-Path $Config 'openomega_launcher.exe'))))
}

$executable = $candidateExecutables |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
    Select-Object -First 1

if ($null -eq $executable) {
    $configSuffix = $Config.ToLowerInvariant()
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("OpenOmega Launcher has not been built for configuration '$Config'.")
    $lines.Add('This shortcut never builds automatically. From the repository root, run one of:')
    if ($Preset -eq 'auto' -or $Preset -eq 'msvc') {
        $lines.Add('')
        $lines.Add('Ninja Multi-Config:')
        $lines.Add('cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && set CC=cl && set CXX=cl && cmake --preset msvc && cmake --build --preset msvc-' +
            $configSuffix + ' --target openomega_launcher"')
    }
    if ($Preset -eq 'auto' -or $Preset -eq 'vs2022-x64') {
        $lines.Add('')
        $lines.Add('Visual Studio 2022 x64:')
        $lines.Add('cmake --preset vs2022-x64')
        $lines.Add('cmake --build --preset vs2022-launcher-debug')
    }
    $lines.Add('')
    $lines.Add('Expected launcher location(s):')
    foreach ($candidate in $candidateExecutables) {
        $lines.Add('  ' + $candidate)
    }
    Write-Error -Message ($lines -join [Environment]::NewLine) -Category ObjectNotFound
    exit 2
}

$resolvedExecutable = [IO.Path]::GetFullPath($executable)
$workingDirectory = Split-Path -Parent $resolvedExecutable
Start-Process -FilePath $resolvedExecutable -WorkingDirectory $workingDirectory | Out-Null
