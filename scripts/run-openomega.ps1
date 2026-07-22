[CmdletBinding()]
param(
    [Alias('Configuration')]
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
    [string]$Config = 'Debug',

    [ValidateSet('auto', 'msvc', 'vs2022-x64')]
    [string]$Preset = 'auto',

    [string]$ConfigFile,

    [Alias('Args')]
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$GameArguments = @()
)

Set-StrictMode -Version 2.0

. (Join-Path $PSScriptRoot 'windows-command-line.ps1')

$repoRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$presetCandidates = if ($Preset -eq 'auto') {
    @('msvc', 'vs2022-x64')
}
else {
    @($Preset)
}

$candidateExecutables = [Collections.Generic.List[object]]::new()
foreach ($candidatePreset in $presetCandidates) {
    $candidateExecutables.Add([pscustomobject]@{
            Preset = $candidatePreset
            Path = Join-Path $repoRoot `
                (Join-Path 'build' `
                    (Join-Path $candidatePreset `
                        (Join-Path 'products\game' `
                            (Join-Path $Config 'openomega.exe'))))
        })
}

# Keep existing Ninja/VS build trees launchable while product output relocation is adopted, but
# never select a legacy artifact ahead of a stable product from another requested preset.
foreach ($candidatePreset in $presetCandidates) {
    $candidateExecutables.Add([pscustomobject]@{
            Preset = $candidatePreset
            Path = Join-Path $repoRoot `
                (Join-Path 'build' `
                    (Join-Path $candidatePreset `
                        (Join-Path $Config 'openomega.exe')))
        })
}

$executable = $null
$selectedPreset = $null
foreach ($candidate in $candidateExecutables) {
    $candidatePath = $candidate.Path
    if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
        $executable = [IO.Path]::GetFullPath($candidatePath)
        $selectedPreset = $candidate.Preset
        break
    }
}

if ($null -eq $executable) {
    $configSuffix = $Config.ToLowerInvariant()
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add("OpenOmega has not been built for configuration '$Config'.")
    $lines.Add('This launcher never builds automatically. From the repository root, run one of:')

    if ($Preset -eq 'auto' -or $Preset -eq 'msvc') {
        $lines.Add('')
        $lines.Add('Ninja Multi-Config:')
        $lines.Add('cmd /c "call ""C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"" && set CC=cl && set CXX=cl && cmake --preset msvc && cmake --build --preset msvc-' +
            $configSuffix + ' --target openomega"')
    }

    if ($Preset -eq 'auto' -or $Preset -eq 'vs2022-x64') {
        $lines.Add('')
        $lines.Add('Visual Studio 2022 x64:')
        $lines.Add('cmake --preset vs2022-x64')
        switch ($Config) {
            'Debug' {
                $lines.Add('cmake --build --preset vs2022-game-debug')
            }
            'RelWithDebInfo' {
                $lines.Add('cmake --build --preset vs2022-game-relwithdebinfo')
            }
            'Release' {
                $lines.Add('cmake --build --preset vs2022-game-release')
            }
        }
    }

    $lines.Add('')
    $lines.Add('Expected executable location(s):')
    foreach ($candidate in $candidateExecutables) {
        $lines.Add('  ' + $candidate.Path)
    }

    Write-Error -Message ($lines -join [Environment]::NewLine) -Category ObjectNotFound
    exit 2
}

$launchArguments = [Collections.Generic.List[string]]::new()
if (-not [string]::IsNullOrWhiteSpace($ConfigFile)) {
    $configPath = if ([IO.Path]::IsPathRooted($ConfigFile)) {
        [IO.Path]::GetFullPath($ConfigFile)
    }
    else {
        [IO.Path]::GetFullPath((Join-Path $repoRoot $ConfigFile))
    }

    if (-not (Test-Path -LiteralPath $configPath -PathType Leaf)) {
        Write-Error -Message "OpenOmega configuration file is missing: $configPath" `
            -Category ObjectNotFound
        exit 2
    }
    $launchArguments.Add('--config=' + $configPath)
}

foreach ($argument in $GameArguments) {
    if ($null -eq $argument) {
        $launchArguments.Add('')
    }
    else {
        $launchArguments.Add([string]$argument)
    }
}

Write-Verbose "Launching $executable (preset $selectedPreset, configuration $Config)."

$maximumStartProcessContentLength = Get-OmegaMaximumWindowsStartProcessContentLength
$encodedExecutableLength = Measure-OmegaWindowsStartProcessFilePath $executable `
    -MaximumEncodedLength $maximumStartProcessContentLength
$maximumArgumentLineLength = $maximumStartProcessContentLength - $encodedExecutableLength - 1
if ($maximumArgumentLineLength -lt 0) {
    throw 'OpenOmega executable path exceeds the bounded Windows launch limit.'
}
$argumentLine = Join-OmegaWindowsCommandLineArguments $launchArguments.ToArray() `
    -MaximumLength $maximumArgumentLineLength

$startParameters = @{
    FilePath = $executable
    WorkingDirectory = $repoRoot
    NoNewWindow = $true
    PassThru = $true
    Wait = $true
}
if ($launchArguments.Count -ne 0) {
    # Windows PowerShell 5.1 flattens ArgumentList. Pass one CRT-encoded line so empty,
    # whitespace-containing, and quote/backslash-bearing values retain exact argv boundaries.
    $startParameters.ArgumentList = $argumentLine
}

$process = $null
try {
    $process = Start-Process @startParameters
    $exitCode = [int]$process.ExitCode
}
finally {
    if ($null -ne $process) {
        $process.Dispose()
    }
}

exit $exitCode
