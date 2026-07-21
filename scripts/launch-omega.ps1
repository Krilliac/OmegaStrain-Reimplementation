[CmdletBinding()]
param(
    [switch]$Debugger,
    [switch]$Resume,
    [switch]$SlowBoot,
    [string]$GameArgs,
    [switch]$DryRun
)

. (Join-Path $PSScriptRoot 'windows-command-line.ps1')

$workspaceRoot = Split-Path -Parent $PSScriptRoot
$pcsx2Exe = Join-Path $workspaceRoot 'third_party\pcsx2\bin\pcsx2-qtx64-avx2.exe'
$dataPath = Join-Path $workspaceRoot 'runtime\data'
$isoPath = Join-Path $workspaceRoot 'private\disc\Syphon Filter - The Omega Strain (USA).iso'
$resumeState = Join-Path $workspaceRoot 'private\sstates\SCUS-97264 (D5605611).resume.p2s'
$logDir = Join-Path $workspaceRoot 'runtime'

foreach ($requiredPath in @($pcsx2Exe, $isoPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required file is missing: $requiredPath"
    }
}

New-Item -ItemType Directory -Path $dataPath, $logDir -Force | Out-Null
$logPath = Join-Path $logDir ("omega-{0}.log" -f (Get-Date -Format 'yyyyMMdd-HHmmss'))

$arguments = @(
    $(if ($SlowBoot) { '-slowboot' } else { '-fastboot' }),
    '-nofullscreen',
    '-datapath', $dataPath,
    '-logfile', $logPath
)

if ($Debugger) {
    $arguments += '-debugger'
}

if (-not [string]::IsNullOrWhiteSpace($GameArgs)) {
    $arguments += @('-gameargs', $GameArgs)
}

if ($Resume) {
    if (-not (Test-Path -LiteralPath $resumeState)) {
        throw "Resume state is missing: $resumeState"
    }
    $arguments += @('-statefile', $resumeState)
}

$arguments += @('--', $isoPath)
$maximumStartProcessContentLength = Get-OmegaMaximumWindowsStartProcessContentLength
# The maximum excludes CreateProcessW's terminating NUL and retains the Windows PowerShell 5.1
# adapter margin. Reserve the always-quoted executable frame plus the one separator before
# Start-Process's flattened argument line.
$encodedExecutableLength = Measure-OmegaWindowsStartProcessFilePath $pcsx2Exe `
    -MaximumEncodedLength $maximumStartProcessContentLength
$maximumArgumentLineLength = $maximumStartProcessContentLength - $encodedExecutableLength - 1
if ($maximumArgumentLineLength -lt 0) {
    throw 'PCSX2 executable path exceeds the bounded Windows launch limit.'
}
# Windows PowerShell flattens Start-Process ArgumentList. Supply one fully encoded line so each
# logical value, especially PCSX2's nested -gameargs payload, survives CreateProcess parsing.
$argumentLine = Join-OmegaWindowsCommandLineArguments $arguments `
    -MaximumLength $maximumArgumentLineLength

if ($DryRun) {
    [pscustomobject]@{
        Executable = $pcsx2Exe
        Arguments = $argumentLine
        Disc = $isoPath
        DataPath = $dataPath
        Log = $logPath
        Debugger = [bool]$Debugger
        Resume = [bool]$Resume
        GameArgs = $GameArgs
    }
    return
}

$process = Start-Process -FilePath $pcsx2Exe -ArgumentList $argumentLine -WindowStyle Normal -PassThru

[pscustomobject]@{
    ProcessId = $process.Id
    Executable = $pcsx2Exe
    Disc = $isoPath
    DataPath = $dataPath
    Log = $logPath
    Debugger = [bool]$Debugger
    Resume = [bool]$Resume
    GameArgs = $GameArgs
}
