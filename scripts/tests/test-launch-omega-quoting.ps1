[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ArgvCaptureExecutable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$scriptsRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $scriptsRoot 'windows-command-line.ps1')

$script:failureCount = 0

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        Write-Error "FAILED: $Message" -ErrorAction Continue
        ++$script:failureCount
    }
}

function Assert-Equal {
    param(
        [AllowEmptyString()]
        [string]$Actual,
        [AllowEmptyString()]
        [string]$Expected,
        [string]$Message
    )

    Assert-True -Condition ($Actual -ceq $Expected) -Message $Message
}

function Assert-Throws {
    param(
        [scriptblock]$Action,
        [string]$Message
    )

    $threw = $false
    try {
        & $Action | Out-Null
    }
    catch {
        $threw = $true
    }
    Assert-True -Condition $threw -Message $Message
}

function Assert-ThrowsExactly {
    param(
        [scriptblock]$Action,
        [string]$ExpectedMessage,
        [string]$Message
    )

    $actualMessage = $null
    try {
        & $Action | Out-Null
    }
    catch {
        $actualMessage = $_.Exception.Message
    }
    Assert-Equal $actualMessage $ExpectedMessage $Message
}

function Assert-SequenceEqual {
    param(
        [string[]]$Actual,
        [string[]]$Expected,
        [string]$Message
    )

    if ($Actual.Count -ne $Expected.Count) {
        Assert-True -Condition $false -Message ($Message + ' (argument count)')
        return
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ($Actual[$index] -cne $Expected[$index]) {
            Assert-True -Condition $false -Message `
                ($Message + ' (argument index ' + $index + ')')
            return
        }
    }
}

function Read-ArgumentCapture {
    param([string]$Path)

    try {
        $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, `
            [IO.FileShare]::Read)
        try {
            $reader = [IO.BinaryReader]::new($stream)
            if ($reader.ReadUInt32() -ne [uint32]0x4752414f -or
                $reader.ReadUInt32() -ne 1) {
                throw 'invalid header'
            }
            $count = $reader.ReadUInt32()
            if ($count -gt 1024) {
                throw 'invalid argument count'
            }

            $arguments = [Collections.Generic.List[string]]::new([int]$count)
            for ($index = 0; $index -lt $count; ++$index) {
                $codeUnits = $reader.ReadUInt32()
                if ($codeUnits -gt (Get-OmegaMaximumWindowsCommandLineLength)) {
                    throw 'invalid argument length'
                }
                $byteCount = [int]$codeUnits * 2
                $bytes = $reader.ReadBytes($byteCount)
                if ($bytes.Length -ne $byteCount) {
                    throw 'truncated argument'
                }
                $arguments.Add([Text.Encoding]::Unicode.GetString($bytes))
            }
            if ($stream.Position -ne $stream.Length) {
                throw 'trailing capture bytes'
            }
            return ,$arguments.ToArray()
        }
        finally {
            $stream.Dispose()
        }
    }
    catch {
        throw 'Argument capture is missing or malformed.'
    }
}

function Wait-ForCapturedProcess {
    param([int]$ProcessId)

    try {
        $process = [Diagnostics.Process]::GetProcessById($ProcessId)
    }
    catch [ArgumentException] {
        return
    }

    try {
        if (-not $process.WaitForExit(10000)) {
            throw 'Synthetic argv capture process timed out.'
        }
        if ($process.ExitCode -ne 0) {
            throw 'Synthetic argv capture process failed.'
        }
    }
    finally {
        $process.Dispose()
    }
}

function Invoke-CapturedLauncher {
    param(
        [string]$Launcher,
        [string]$CapturePath,
        [object[]]$LaunchArguments
    )

    if (Test-Path -LiteralPath $CapturePath) {
        Remove-Item -LiteralPath $CapturePath -Force
    }
    $priorCapturePath = [Environment]::GetEnvironmentVariable(
        'OPENOMEGA_ARGV_CAPTURE_PATH', [EnvironmentVariableTarget]::Process)
    try {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_ARGV_CAPTURE_PATH', $CapturePath,
            [EnvironmentVariableTarget]::Process)
        $launchResult = & $Launcher @LaunchArguments
        Wait-ForCapturedProcess -ProcessId $launchResult.ProcessId
    }
    finally {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_ARGV_CAPTURE_PATH', $priorCapturePath,
            [EnvironmentVariableTarget]::Process)
    }

    [pscustomobject]@{
        Launch = $launchResult
        Arguments = [string[]](Read-ArgumentCapture $CapturePath)
    }
}

if (-not [IO.Path]::IsPathRooted($ArgvCaptureExecutable) -or
    -not (Test-Path -LiteralPath $ArgvCaptureExecutable -PathType Leaf)) {
    throw 'ArgvCaptureExecutable must be an absolute existing file.'
}

$quote = [string][char]34
$backslash = [string][char]92

Assert-Equal (Measure-OmegaWindowsStartProcessFilePath 'plain.exe') '11' `
    'Start-Process executable framing always reserves both quotes'
Assert-Equal (Measure-OmegaWindowsStartProcessFilePath 'two words.exe') '15' `
    'Start-Process executable framing includes quotes around spaced paths'
Assert-Throws { Measure-OmegaWindowsStartProcessFilePath ('bad' + $quote + '.exe') } `
    'quoted executable paths fail closed'
Assert-Throws { Measure-OmegaWindowsStartProcessFilePath ('bad' + [char]0 + '.exe') } `
    'NUL executable paths fail closed'
$maximumExecutablePath = ''.PadLeft(
    (Get-OmegaMaximumWindowsCommandLineLength) - 2, [char]97)
Assert-Equal (Measure-OmegaWindowsStartProcessFilePath $maximumExecutablePath) `
    (Get-OmegaMaximumWindowsCommandLineLength) `
    'an always-quoted executable at the content bound is accepted'
Assert-Throws { Measure-OmegaWindowsStartProcessFilePath ($maximumExecutablePath + 'a') } `
    'an always-quoted executable over the content bound fails closed'

Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument '') ($quote + $quote) `
    'empty arguments retain an explicit argv slot'
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument 'plain') 'plain' `
    'plain arguments remain unquoted'
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument 'two words') `
    ($quote + 'two words' + $quote) 'spaces force one quoted argument'
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument ('a' + $quote + 'b')) `
    ($quote + 'a' + $backslash + $quote + 'b' + $quote) `
    'embedded quotes receive one escaping backslash'

$threeBackslashes = ''.PadLeft(3, [char]92)
$sevenBackslashes = ''.PadLeft(7, [char]92)
Assert-Equal `
    (ConvertTo-OmegaWindowsCommandLineArgument ('left' + $threeBackslashes + $quote + 'right')) `
    ($quote + 'left' + $sevenBackslashes + $quote + 'right' + $quote) `
    'backslash runs before quotes are doubled with one quote escape'

$twoBackslashes = ''.PadLeft(2, [char]92)
$fourBackslashes = ''.PadLeft(4, [char]92)
$terminalBackslashArgument = 'C:\path with space' + $twoBackslashes
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument $terminalBackslashArgument) `
    ($quote + 'C:\path with space' + $fourBackslashes + $quote) `
    'terminal backslashes are doubled before the closing quote'

$hostilePrefix = '--flag=$(Write-Output pwned); & calc.exe '
$hostileArgument = $hostilePrefix + $quote + 'quoted' + $quote + $backslash + 'tail'
$hostileExpected = $quote + $hostilePrefix + $backslash + $quote + 'quoted' + `
    $backslash + $quote + $backslash + 'tail' + $quote
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument $hostileArgument) $hostileExpected `
    'hostile-looking values remain one inert encoded argument'

$joinedExpected = 'plain ' + ($quote + $quote) + ' ' + `
    ($quote + 'two words' + $quote) + ' ' + $hostileExpected
Assert-Equal (Join-OmegaWindowsCommandLineArguments @('plain', '', 'two words', $hostileArgument)) `
    $joinedExpected 'joined argv preserves empty, spaced, and hostile-looking values'

$maximumPlain = ''.PadLeft((Get-OmegaMaximumWindowsCommandLineLength), [char]97)
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument $maximumPlain) $maximumPlain `
    'the exact unquoted command-line bound is accepted'
Assert-Throws { ConvertTo-OmegaWindowsCommandLineArgument ($maximumPlain + 'a') } `
    'arguments beyond the command-line bound fail closed'
Assert-Throws { ConvertTo-OmegaWindowsCommandLineArgument ('a' + [char]0 + 'b') } `
    'NUL arguments fail closed'
Assert-Throws { Join-OmegaWindowsCommandLineArguments @('ab', 'cd') -MaximumLength 4 } `
    'joined command lines beyond their configured bound fail closed'

$temporaryBase = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\')
$temporaryRoot = Join-Path $temporaryBase `
    ('omega-launch-quoting-' + [guid]::NewGuid().ToString('N') + ' workspace with spaces')
$resolvedTemporaryRoot = [IO.Path]::GetFullPath($temporaryRoot)
$requiredPrefix = $temporaryBase + '\omega-launch-quoting-'
if (-not $resolvedTemporaryRoot.StartsWith(
        $requiredPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Synthetic launcher root escaped the expected temporary directory.'
}

try {
    $syntheticScripts = Join-Path $resolvedTemporaryRoot 'scripts'
    $syntheticPcsx2Directory = Join-Path $resolvedTemporaryRoot 'third_party\pcsx2\bin'
    $syntheticDiscDirectory = Join-Path $resolvedTemporaryRoot 'private\disc'
    $syntheticStateDirectory = Join-Path $resolvedTemporaryRoot 'private\sstates'
    $syntheticCaptureDirectory = Join-Path $resolvedTemporaryRoot 'runtime\argv-captures'
    New-Item -ItemType Directory -Path $syntheticScripts, $syntheticPcsx2Directory, `
        $syntheticDiscDirectory, $syntheticStateDirectory, $syntheticCaptureDirectory `
        -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'launch-omega.ps1') `
        -Destination $syntheticScripts
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'windows-command-line.ps1') `
        -Destination $syntheticScripts

    $syntheticPcsx2 = Join-Path $syntheticPcsx2Directory 'pcsx2-qtx64-avx2.exe'
    $syntheticIso = Join-Path $syntheticDiscDirectory `
        'Syphon Filter - The Omega Strain (USA).iso'
    $syntheticState = Join-Path $syntheticStateDirectory `
        'SCUS-97264 (D5605611).resume.p2s'
    Copy-Item -LiteralPath $ArgvCaptureExecutable -Destination $syntheticPcsx2
    [IO.File]::WriteAllBytes($syntheticIso, [byte[]]@())
    [IO.File]::WriteAllBytes($syntheticState, [byte[]]@())

    $syntheticLauncher = Join-Path $syntheticScripts 'launch-omega.ps1'
    $dryRun = & $syntheticLauncher -DryRun -GameArgs $hostileArgument
    $dryRunArguments = @(
        '-fastboot',
        '-nofullscreen',
        '-datapath', (Join-Path $resolvedTemporaryRoot 'runtime\data'),
        '-logfile', $dryRun.Log,
        '-gameargs', $hostileArgument,
        '--', $syntheticIso
    )
    $encodedExecutableLength = Measure-OmegaWindowsStartProcessFilePath $syntheticPcsx2
    $maximumArgumentLineLength = (Get-OmegaMaximumWindowsCommandLineLength) - `
        $encodedExecutableLength - 1
    $expectedDryRunArguments = Join-OmegaWindowsCommandLineArguments $dryRunArguments `
        -MaximumLength $maximumArgumentLineLength
    Assert-Equal $dryRun.Arguments $expectedDryRunArguments `
        'dry-run launcher preserves hostile-looking GameArgs as one encoded PCSX2 argument'
    Assert-Equal $dryRun.GameArgs $hostileArgument `
        'dry-run launcher reports the unchanged logical GameArgs value'

    $emptyDryRun = & $syntheticLauncher -DryRun -GameArgs ''
    Assert-True -Condition (-not $emptyDryRun.Arguments.Contains('-gameargs')) `
        -Message 'empty GameArgs preserve the historical omission behavior'

    $optionDryRun = & $syntheticLauncher -DryRun -SlowBoot -Debugger -Resume
    $optionArguments = @(
        '-slowboot',
        '-nofullscreen',
        '-datapath', (Join-Path $resolvedTemporaryRoot 'runtime\data'),
        '-logfile', $optionDryRun.Log,
        '-debugger',
        '-statefile', $syntheticState,
        '--', $syntheticIso
    )
    $expectedOptionArguments = Join-OmegaWindowsCommandLineArguments $optionArguments `
        -MaximumLength $maximumArgumentLineLength
    Assert-Equal $optionDryRun.Arguments $expectedOptionArguments `
        'slow boot, debugger, and resume preserve their historical argv ordering'
    Assert-True -Condition ($optionDryRun.Debugger -and $optionDryRun.Resume) `
        -Message 'dry-run option metadata preserves debugger and resume state'

    $sentinelPath = Join-Path $syntheticCaptureDirectory 'hostile-text-was-evaluated'
    $priorSentinelPath = [Environment]::GetEnvironmentVariable(
        'OPENOMEGA_QUOTING_SENTINEL', [EnvironmentVariableTarget]::Process)
    try {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_QUOTING_SENTINEL', $sentinelPath,
            [EnvironmentVariableTarget]::Process)
        $roundTripGameArgs = `
            '$(Set-Content -LiteralPath $env:OPENOMEGA_QUOTING_SENTINEL -Value pwned); ' + `
            '& Write-Output injected ' + $quote + 'quoted' + $quote + '-slash-' + `
            $threeBackslashes + $quote + 'tail-' + $twoBackslashes
        $roundTrip = Invoke-CapturedLauncher -Launcher $syntheticLauncher `
            -CapturePath (Join-Path $syntheticCaptureDirectory 'round-trip.argv') `
            -LaunchArguments @('-SlowBoot', '-Debugger', '-Resume', '-GameArgs', `
                $roundTripGameArgs)
    }
    finally {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_QUOTING_SENTINEL', $priorSentinelPath,
            [EnvironmentVariableTarget]::Process)
    }
    $roundTripExpected = @(
        $syntheticPcsx2,
        '-slowboot',
        '-nofullscreen',
        '-datapath', (Join-Path $resolvedTemporaryRoot 'runtime\data'),
        '-logfile', $roundTrip.Launch.Log,
        '-debugger',
        '-gameargs', $roundTripGameArgs,
        '-statefile', $syntheticState,
        '--', $syntheticIso
    )
    Assert-SequenceEqual -Actual $roundTrip.Arguments -Expected $roundTripExpected `
        -Message 'the launched child receives the exact logical argv sequence'
    Assert-True -Condition (-not (Test-Path -LiteralPath $sentinelPath)) `
        -Message 'hostile-looking GameArgs text is never evaluated'

    # Grow a plain GameArgs value by the exact remaining encoded capacity. This keeps the
    # oracle independent of temporary-path length while exercising the launcher's full command
    # construction, including the executable frame, separator, and terminating-NUL allowance.
    $boundarySeed = & $syntheticLauncher -DryRun -GameArgs 'a'
    $remainingCapacity = $maximumArgumentLineLength - $boundarySeed.Arguments.Length
    Assert-True -Condition ($remainingCapacity -gt 0) `
        -Message 'the synthetic launcher has positive boundary-test capacity'
    $exactGameArgs = ''.PadLeft(1 + $remainingCapacity, [char]97)
    $exactDryRun = & $syntheticLauncher -DryRun -GameArgs $exactGameArgs
    Assert-Equal $exactDryRun.Arguments.Length $maximumArgumentLineLength `
        'the exact accepted argument line reaches its deterministic bound'
    Assert-Equal ($encodedExecutableLength + 1 + $exactDryRun.Arguments.Length) `
        (Get-OmegaMaximumWindowsCommandLineLength) `
        'the exact accepted launch consumes the full CreateProcess content budget'
    Assert-ThrowsExactly `
        { & $syntheticLauncher -DryRun -GameArgs ($exactGameArgs + 'a') } `
        'Windows command line exceeds the bounded launch limit.' `
        'one code unit beyond the total command-line bound is rejected deterministically'

    $exactRoundTrip = Invoke-CapturedLauncher -Launcher $syntheticLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'exact-boundary.argv') `
        -LaunchArguments @('-GameArgs', $exactGameArgs)
    $exactExpected = @(
        $syntheticPcsx2,
        '-fastboot',
        '-nofullscreen',
        '-datapath', (Join-Path $resolvedTemporaryRoot 'runtime\data'),
        '-logfile', $exactRoundTrip.Launch.Log,
        '-gameargs', $exactGameArgs,
        '--', $syntheticIso
    )
    Assert-SequenceEqual -Actual $exactRoundTrip.Arguments -Expected $exactExpected `
        -Message 'the exact accepted command-line boundary reaches the launched child intact'

    Remove-Item -LiteralPath $syntheticIso -Force
    Assert-Throws { & $syntheticLauncher -DryRun | Out-Null } `
        'dry-run launcher still fails closed when a required file is absent'
}
finally {
    if (Test-Path -LiteralPath $resolvedTemporaryRoot) {
        Remove-Item -LiteralPath $resolvedTemporaryRoot -Recurse -Force
    }
}

if ($script:failureCount -ne 0) {
    throw "$script:failureCount launch-omega quoting test(s) failed."
}
Write-Output 'test-launch-omega-quoting: all checks passed'
