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
        $actualJson = ConvertTo-Json -InputObject @($Actual) -Compress
        $expectedJson = ConvertTo-Json -InputObject @($Expected) -Compress
        Assert-True -Condition $false -Message ($Message +
            " (argument count: actual=$($Actual.Count) expected=$($Expected.Count); " +
            "actual=$actualJson expected=$expectedJson)")
        return
    }
    for ($index = 0; $index -lt $Expected.Count; ++$index) {
        if ($Actual[$index] -cne $Expected[$index]) {
            Assert-True -Condition $false -Message `
                ($Message + ' (argument index ' + $index + ': actual=' +
                    (ConvertTo-Json $Actual[$index] -Compress) + ' expected=' +
                    (ConvertTo-Json $Expected[$index] -Compress) + ')')
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
                if ($codeUnits -gt (Get-OmegaMaximumWindowsStartProcessContentLength)) {
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
        # A Process reopened by ID does not reliably expose ExitCode in Windows PowerShell 5.1.
        # Read-ArgumentCapture below is the authoritative success and integrity check.
    }
    finally {
        $process.Dispose()
    }
}

function Invoke-CapturedLauncher {
    param(
        [string]$Launcher,
        [string]$CapturePath,
        [hashtable]$LaunchParameters
    )

    if (Test-Path -LiteralPath $CapturePath) {
        Remove-Item -LiteralPath $CapturePath -Force
    }
    $priorCapturePath = [Environment]::GetEnvironmentVariable(
        'OPENOMEGA_ARGV_CAPTURE_PATH', [EnvironmentVariableTarget]::Process)
    try {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_ARGV_CAPTURE_PATH', $CapturePath,
            [EnvironmentVariableTarget]::Process)
        $launchResult = & $Launcher @LaunchParameters
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

function Invoke-NativeLauncherProcess {
    param(
        [string]$PowerShellExecutable,
        [string]$Driver,
        [string]$Launcher,
        [string]$CapturePath,
        [string]$ArgumentsPath,
        [ValidateSet('auto', 'msvc', 'vs2022-x64')]
        [string]$Preset = 'auto',
        [ValidateSet('Debug', 'RelWithDebInfo', 'Release')]
        [string]$Config = 'Debug',
        [AllowEmptyString()]
        [string]$ConfigFile = '__OPENOMEGA_NO_CONFIG__',
        [string[]]$GameArguments = @()
    )

    if (Test-Path -LiteralPath $CapturePath) {
        Remove-Item -LiteralPath $CapturePath -Force
    }
    $standardOutputPath = $CapturePath + '.stdout'
    $standardErrorPath = $CapturePath + '.stderr'
    foreach ($outputPath in @($standardOutputPath, $standardErrorPath)) {
        if (Test-Path -LiteralPath $outputPath) {
            Remove-Item -LiteralPath $outputPath -Force
        }
    }

    $argumentsJson = ConvertTo-Json -InputObject @($GameArguments) -Compress
    [IO.File]::WriteAllText(
        $ArgumentsPath, $argumentsJson, [Text.UTF8Encoding]::new($false))

    $driverArguments = @(
        '-NoLogo',
        '-NoProfile',
        '-File', $Driver,
        '-Launcher', $Launcher,
        '-Preset', $Preset,
        '-Config', $Config,
        '-ConfigFile', $ConfigFile,
        '-ArgumentsPath', $ArgumentsPath
    )
    $maximumContentLength = Get-OmegaMaximumWindowsStartProcessContentLength
    $encodedPowerShellLength = Measure-OmegaWindowsStartProcessFilePath `
        $PowerShellExecutable -MaximumEncodedLength $maximumContentLength
    $maximumDriverArgumentLength = $maximumContentLength - $encodedPowerShellLength - 1
    $driverArgumentLine = Join-OmegaWindowsCommandLineArguments $driverArguments `
        -MaximumLength $maximumDriverArgumentLength

    $priorCapturePath = [Environment]::GetEnvironmentVariable(
        'OPENOMEGA_ARGV_CAPTURE_PATH', [EnvironmentVariableTarget]::Process)
    $process = $null
    try {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_ARGV_CAPTURE_PATH', $CapturePath,
            [EnvironmentVariableTarget]::Process)
        $process = Start-Process -FilePath $PowerShellExecutable `
            -ArgumentList $driverArgumentLine -NoNewWindow -PassThru -Wait `
            -RedirectStandardOutput $standardOutputPath `
            -RedirectStandardError $standardErrorPath
        $exitCode = [int]$process.ExitCode
    }
    finally {
        [Environment]::SetEnvironmentVariable('OPENOMEGA_ARGV_CAPTURE_PATH', $priorCapturePath,
            [EnvironmentVariableTarget]::Process)
        if ($null -ne $process) {
            $process.Dispose()
        }
    }

    $capturedArguments = $null
    if (Test-Path -LiteralPath $CapturePath -PathType Leaf) {
        $capturedArguments = [string[]](Read-ArgumentCapture $CapturePath)
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Arguments = $capturedArguments
        StandardOutput = if (Test-Path -LiteralPath $standardOutputPath) {
            [IO.File]::ReadAllText($standardOutputPath)
        } else { '' }
        StandardError = if (Test-Path -LiteralPath $standardErrorPath) {
            [IO.File]::ReadAllText($standardErrorPath)
        } else { '' }
    }
}

if (-not [IO.Path]::IsPathRooted($ArgvCaptureExecutable) -or
    -not (Test-Path -LiteralPath $ArgvCaptureExecutable -PathType Leaf)) {
    throw 'ArgvCaptureExecutable must be an absolute existing file.'
}

$quote = [string][char]34
$backslash = [string][char]92

Assert-Equal (Get-OmegaMaximumWindowsStartProcessContentLength) '32765' `
    'the Windows PowerShell 5.1 Start-Process content ceiling remains pinned'
Assert-Equal (Measure-OmegaWindowsStartProcessFilePath 'plain.exe') '11' `
    'Start-Process executable framing always reserves both quotes'
Assert-Equal (Measure-OmegaWindowsStartProcessFilePath 'two words.exe') '15' `
    'Start-Process executable framing includes quotes around spaced paths'
Assert-Throws { Measure-OmegaWindowsStartProcessFilePath ('bad' + $quote + '.exe') } `
    'quoted executable paths fail closed'
Assert-Throws { Measure-OmegaWindowsStartProcessFilePath ('bad' + [char]0 + '.exe') } `
    'NUL executable paths fail closed'
$maximumExecutablePath = ''.PadLeft(
    (Get-OmegaMaximumWindowsStartProcessContentLength) - 2, [char]97)
Assert-Equal (Measure-OmegaWindowsStartProcessFilePath $maximumExecutablePath) `
    (Get-OmegaMaximumWindowsStartProcessContentLength) `
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

$maximumPlain = ''.PadLeft((Get-OmegaMaximumWindowsStartProcessContentLength), [char]97)
Assert-Equal (ConvertTo-OmegaWindowsCommandLineArgument $maximumPlain) $maximumPlain `
    'the exact unquoted Start-Process adapter bound is accepted'
Assert-Throws { ConvertTo-OmegaWindowsCommandLineArgument ($maximumPlain + 'a') } `
    'arguments beyond the Start-Process adapter bound fail closed'
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
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'launch-pcsx2-reference.ps1') `
        -Destination $syntheticScripts
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'run-openomega.ps1') `
        -Destination $syntheticScripts
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'windows-command-line.ps1') `
        -Destination $syntheticScripts

    $nativeLauncherDriver = Join-Path $syntheticScripts 'invoke-native-launcher-test-driver.ps1'
    [IO.File]::WriteAllText($nativeLauncherDriver, @'
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Launcher,
    [Parameter(Mandatory = $true)]
    [string]$Preset,
    [Parameter(Mandatory = $true)]
    [string]$Config,
    [Parameter(Mandatory = $true)]
    [AllowEmptyString()]
    [string]$ConfigFile,
    [Parameter(Mandatory = $true)]
    [string]$ArgumentsPath
)

Set-StrictMode -Version Latest
$decodedArguments = ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($ArgumentsPath))
$gameArgumentList = [Collections.Generic.List[string]]::new()
foreach ($argument in $decodedArguments) {
    $gameArgumentList.Add([string]$argument)
}
$parameters = @{
    Preset = $Preset
    Config = $Config
    GameArguments = $gameArgumentList.ToArray()
}
if ($ConfigFile -cne '__OPENOMEGA_NO_CONFIG__') {
    $parameters.ConfigFile = $ConfigFile
}
& $Launcher @parameters
exit $LASTEXITCODE
'@, [Text.UTF8Encoding]::new($false))

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
    $maximumArgumentLineLength = (Get-OmegaMaximumWindowsStartProcessContentLength) - `
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
            -LaunchParameters @{
                SlowBoot = $true
                Debugger = $true
                Resume = $true
                GameArgs = $roundTripGameArgs
            }
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
    # construction, including the executable frame, separator, terminating-NUL allowance, and
    # the Windows PowerShell 5.1 adapter margin.
    $boundarySeed = & $syntheticLauncher -DryRun -GameArgs 'a'
    $remainingCapacity = $maximumArgumentLineLength - $boundarySeed.Arguments.Length
    Assert-True -Condition ($remainingCapacity -gt 0) `
        -Message 'the synthetic launcher has positive boundary-test capacity'
    $exactGameArgs = ''.PadLeft(1 + $remainingCapacity, [char]97)
    $exactDryRun = & $syntheticLauncher -DryRun -GameArgs $exactGameArgs
    Assert-Equal $exactDryRun.Arguments.Length $maximumArgumentLineLength `
        'the exact accepted argument line reaches its deterministic bound'
    Assert-Equal ($encodedExecutableLength + 1 + $exactDryRun.Arguments.Length) `
        (Get-OmegaMaximumWindowsStartProcessContentLength) `
        'the exact accepted launch consumes the bounded Start-Process content budget'
    Assert-ThrowsExactly `
        { & $syntheticLauncher -DryRun -GameArgs ($exactGameArgs + 'a') } `
        'Windows command line exceeds the bounded launch limit.' `
        'one code unit beyond the total command-line bound is rejected deterministically'

    $exactRoundTrip = Invoke-CapturedLauncher -Launcher $syntheticLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'exact-boundary.argv') `
        -LaunchParameters @{ GameArgs = $exactGameArgs }
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
        -Message 'the exact accepted Start-Process adapter boundary reaches the child intact'

    $nativeLauncher = Join-Path $syntheticScripts 'run-openomega.ps1'
    $currentPowerShellProcess = [Diagnostics.Process]::GetCurrentProcess()
    try {
        $nativePowerShell = $currentPowerShellProcess.MainModule.FileName
    }
    finally {
        $currentPowerShellProcess.Dispose()
    }
    $nativeArgumentsPath = Join-Path $syntheticCaptureDirectory 'native-arguments.json'
    $stableMsvcDirectory = Join-Path $resolvedTemporaryRoot `
        'build\msvc\products\game\Debug'
    $stableVisualStudioDirectory = Join-Path $resolvedTemporaryRoot `
        'build\vs2022-x64\products\game\Debug'
    $legacyMsvcDirectory = Join-Path $resolvedTemporaryRoot 'build\msvc\Debug'
    $stableMsvcExecutable = Join-Path $stableMsvcDirectory 'openomega.exe'
    $stableVisualStudioExecutable = Join-Path $stableVisualStudioDirectory 'openomega.exe'
    $legacyMsvcExecutable = Join-Path $legacyMsvcDirectory 'openomega.exe'
    New-Item -ItemType Directory -Path $stableMsvcDirectory, `
        $stableVisualStudioDirectory, $legacyMsvcDirectory -Force | Out-Null

    # A stable product from the second automatic preset must win over a legacy
    # artifact from the first preset.
    Copy-Item -LiteralPath $ArgvCaptureExecutable -Destination $legacyMsvcExecutable
    Copy-Item -LiteralPath $ArgvCaptureExecutable -Destination $stableVisualStudioExecutable
    $stableVisualStudioPreference = Invoke-NativeLauncherProcess `
        -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
        -Launcher $nativeLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'native-stable-vs.argv') `
        -ArgumentsPath $nativeArgumentsPath -GameArguments @('--stable-vs')
    Assert-True -Condition ($stableVisualStudioPreference.ExitCode -eq 0) `
        -Message 'the stable Visual Studio product exits successfully'
    if ($null -ne $stableVisualStudioPreference.Arguments) {
        Assert-SequenceEqual -Actual $stableVisualStudioPreference.Arguments `
            -Expected @($stableVisualStudioExecutable, '--stable-vs') `
            -Message 'automatic selection prefers every stable product over legacy output'
    } else {
        Assert-True -Condition $false `
            -Message 'the stable Visual Studio product writes an argv capture'
    }

    # The first automatic preset wins when both stable products exist, while
    # config and game arguments retain exact argv boundaries.
    Copy-Item -LiteralPath $ArgvCaptureExecutable -Destination $stableMsvcExecutable
    $nativeConfigRelative = 'config files\native test.cfg'
    $nativeConfigAbsolute = Join-Path $resolvedTemporaryRoot $nativeConfigRelative
    New-Item -ItemType Directory -Path (Split-Path -Parent $nativeConfigAbsolute) `
        -Force | Out-Null
    [IO.File]::WriteAllText($nativeConfigAbsolute, 'synthetic=true')
    $nativeSentinelPath = Join-Path $syntheticCaptureDirectory `
        'native-hostile-text-was-evaluated'
    $priorNativeSentinelPath = [Environment]::GetEnvironmentVariable(
        'OPENOMEGA_NATIVE_QUOTING_SENTINEL', [EnvironmentVariableTarget]::Process)
    try {
        [Environment]::SetEnvironmentVariable(
            'OPENOMEGA_NATIVE_QUOTING_SENTINEL', $nativeSentinelPath,
            [EnvironmentVariableTarget]::Process)
        $nativeHostileArgument = `
            '$(Set-Content -LiteralPath $env:OPENOMEGA_NATIVE_QUOTING_SENTINEL ' + `
            '-Value pwned); & Write-Output injected'
        $nativeRoundTripArguments = @(
            $nativeHostileArgument,
            '',
            'two words',
            $terminalBackslashArgument
        )
        $nativeRoundTrip = Invoke-NativeLauncherProcess `
            -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
            -Launcher $nativeLauncher `
            -CapturePath (Join-Path $syntheticCaptureDirectory 'native-round-trip.argv') `
            -ArgumentsPath $nativeArgumentsPath -ConfigFile $nativeConfigRelative `
            -GameArguments $nativeRoundTripArguments
    }
    finally {
        [Environment]::SetEnvironmentVariable(
            'OPENOMEGA_NATIVE_QUOTING_SENTINEL', $priorNativeSentinelPath,
            [EnvironmentVariableTarget]::Process)
    }
    Assert-True -Condition ($nativeRoundTrip.ExitCode -eq 0) `
        -Message 'the stable Ninja product exits successfully'
    if ($null -ne $nativeRoundTrip.Arguments) {
        Assert-SequenceEqual -Actual $nativeRoundTrip.Arguments -Expected @(
            $stableMsvcExecutable,
            ('--config=' + $nativeConfigAbsolute),
            $nativeHostileArgument,
            '',
            'two words',
            $terminalBackslashArgument
        ) -Message 'native config, hostile, empty, spaced, and slash arguments round-trip exactly'
    } else {
        Assert-True -Condition $false `
            -Message 'the stable Ninja product writes an argv capture'
    }
    Assert-True -Condition (-not (Test-Path -LiteralPath $nativeSentinelPath)) `
        -Message 'native hostile-looking argument text is never evaluated'

    # With no stable products, automatic selection falls back to the first
    # preset's legacy artifact.
    Remove-Item -LiteralPath $stableMsvcExecutable, $stableVisualStudioExecutable -Force
    $legacyFallback = Invoke-NativeLauncherProcess `
        -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
        -Launcher $nativeLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'native-legacy.argv') `
        -ArgumentsPath $nativeArgumentsPath -GameArguments @('--legacy')
    Assert-True -Condition ($legacyFallback.ExitCode -eq 0) `
        -Message 'the legacy Ninja fallback exits successfully'
    if ($null -ne $legacyFallback.Arguments) {
        Assert-SequenceEqual -Actual $legacyFallback.Arguments `
            -Expected @($legacyMsvcExecutable, '--legacy') `
            -Message 'automatic selection falls back to legacy output only after stable products'
    } else {
        Assert-True -Condition $false `
            -Message 'the legacy Ninja fallback writes an argv capture'
    }

    Remove-Item -LiteralPath $legacyMsvcExecutable -Force
    $missingNativeBinary = Invoke-NativeLauncherProcess `
        -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
        -Launcher $nativeLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'native-missing-binary.argv') `
        -ArgumentsPath $nativeArgumentsPath
    Assert-True -Condition ($missingNativeBinary.ExitCode -eq 2 -and
        $null -eq $missingNativeBinary.Arguments) `
        -Message ('a missing native binary fails closed with exit code 2 before launch ' +
            "(actual=$($missingNativeBinary.ExitCode))")

    Copy-Item -LiteralPath $ArgvCaptureExecutable -Destination $stableMsvcExecutable
    $missingNativeConfig = Invoke-NativeLauncherProcess `
        -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
        -Launcher $nativeLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'native-missing-config.argv') `
        -ArgumentsPath $nativeArgumentsPath `
        -ConfigFile 'missing config\openomega.cfg'
    Assert-True -Condition ($missingNativeConfig.ExitCode -eq 2 -and
        $null -eq $missingNativeConfig.Arguments) `
        -Message ('a missing native config fails closed with exit code 2 before launch ' +
            "(actual=$($missingNativeConfig.ExitCode))")

    # A renamed command processor provides a deterministic nonzero child without
    # requiring another compiled fixture.
    Copy-Item -LiteralPath $env:ComSpec -Destination $stableMsvcExecutable -Force
    $nativeChildFailure = Invoke-NativeLauncherProcess `
        -PowerShellExecutable $nativePowerShell -Driver $nativeLauncherDriver `
        -Launcher $nativeLauncher `
        -CapturePath (Join-Path $syntheticCaptureDirectory 'native-child-exit.argv') `
        -ArgumentsPath $nativeArgumentsPath -GameArguments @('/d', '/c', 'exit', '7')
    Assert-True -Condition ($nativeChildFailure.ExitCode -eq 7) `
        -Message ('the native launcher propagates its child process exit code exactly ' +
            "(actual=$($nativeChildFailure.ExitCode))")

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
