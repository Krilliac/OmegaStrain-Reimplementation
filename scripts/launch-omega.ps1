[CmdletBinding()]
param(
    [switch]$Debugger,
    [switch]$Resume,
    [switch]$SlowBoot,
    [string]$GameArgs,
    [switch]$DryRun
)

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
    '-datapath', ('"{0}"' -f $dataPath),
    '-logfile', ('"{0}"' -f $logPath)
)

if ($Debugger) {
    $arguments += '-debugger'
}

if (-not [string]::IsNullOrWhiteSpace($GameArgs)) {
    if ($GameArgs.Contains('"')) {
        throw 'GameArgs cannot contain a double quote.'
    }
    $arguments += @('-gameargs', ('"{0}"' -f $GameArgs))
}

if ($Resume) {
    if (-not (Test-Path -LiteralPath $resumeState)) {
        throw "Resume state is missing: $resumeState"
    }
    $arguments += @('-statefile', ('"{0}"' -f $resumeState))
}

$arguments += @('--', ('"{0}"' -f $isoPath))
$argumentLine = $arguments -join ' '

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
