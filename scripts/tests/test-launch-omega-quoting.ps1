[CmdletBinding()]
param()

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

$quote = [string][char]34
$backslash = [string][char]92

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
    ('omega-launch-quoting-' + [guid]::NewGuid().ToString('N'))
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
    New-Item -ItemType Directory -Path $syntheticScripts, $syntheticPcsx2Directory, `
        $syntheticDiscDirectory, $syntheticStateDirectory -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'launch-omega.ps1') `
        -Destination $syntheticScripts
    Copy-Item -LiteralPath (Join-Path $scriptsRoot 'windows-command-line.ps1') `
        -Destination $syntheticScripts

    $syntheticPcsx2 = Join-Path $syntheticPcsx2Directory 'pcsx2-qtx64-avx2.exe'
    $syntheticIso = Join-Path $syntheticDiscDirectory `
        'Syphon Filter - The Omega Strain (USA).iso'
    $syntheticState = Join-Path $syntheticStateDirectory `
        'SCUS-97264 (D5605611).resume.p2s'
    [IO.File]::WriteAllBytes($syntheticPcsx2, [byte[]]@())
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
    $encodedExecutable = ConvertTo-OmegaWindowsCommandLineArgument $syntheticPcsx2
    $maximumArgumentLineLength = (Get-OmegaMaximumWindowsCommandLineLength) - `
        $encodedExecutable.Length - 1
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
