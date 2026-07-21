function Get-OmegaMaximumWindowsCommandLineLength {
    [CmdletBinding()]
    [OutputType([int])]
    param()

    # CreateProcessW accepts at most 32,767 UTF-16 code units including the terminating NUL.
    return 32766
}

function Measure-OmegaWindowsStartProcessFilePath {
    [CmdletBinding()]
    [OutputType([int])]
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [ValidateNotNullOrEmpty()]
        [string]$FilePath,

        [ValidateRange(0, 32766)]
        [int]$MaximumEncodedLength = 32766
    )

    # Start-Process supplies FilePath separately, but its Windows process construction frames the
    # executable as argv[0]. Always reserving both quotes is exact for that construction and remains
    # conservative when ShellExecute can omit them. Quotes cannot occur in a valid Windows path.
    foreach ($character in $FilePath.ToCharArray()) {
        if ($character -eq [char]0) {
            throw 'Windows executable paths cannot contain NUL.'
        }
        if ($character -eq [char]34) {
            throw 'Windows executable paths cannot contain a double quote.'
        }
    }

    [long]$encodedLength = [long]$FilePath.Length + 2
    if ($encodedLength -gt $MaximumEncodedLength) {
        throw 'Executable path exceeds the bounded Windows launch limit.'
    }
    return [int]$encodedLength
}

function ConvertTo-OmegaWindowsCommandLineArgument {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [AllowEmptyString()]
        [string]$Argument,

        [ValidateRange(0, 32766)]
        [int]$MaximumEncodedLength = 32766
    )

    if ($Argument.Length -gt $MaximumEncodedLength) {
        throw 'Windows command-line argument exceeds the bounded launch limit.'
    }

    $requiresQuoting = $Argument.Length -eq 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]0) {
            throw 'Windows command-line arguments cannot contain NUL.'
        }
        if ($character -eq [char]34 -or [char]::IsWhiteSpace($character)) {
            $requiresQuoting = $true
        }
    }

    if (-not $requiresQuoting) {
        return $Argument
    }

    # The CRT/CommandLineToArgvW-compatible quoted form doubles every run of backslashes that
    # precedes a quote, adds one more backslash for the quote itself, and doubles terminal
    # backslashes before the closing quote.
    [long]$encodedLength = 2
    [long]$backslashRun = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]92) {
            ++$backslashRun
            continue
        }

        if ($character -eq [char]34) {
            $encodedLength += (2 * $backslashRun) + 2
        }
        else {
            $encodedLength += $backslashRun + 1
        }
        $backslashRun = 0
    }
    $encodedLength += 2 * $backslashRun

    if ($encodedLength -gt $MaximumEncodedLength) {
        throw 'Windows command-line argument exceeds the bounded launch limit.'
    }

    $builder = [System.Text.StringBuilder]::new([int]$encodedLength)
    $null = $builder.Append([char]34)
    [int]$backslashRun = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq [char]92) {
            ++$backslashRun
            continue
        }

        if ($character -eq [char]34) {
            if ($backslashRun -ne 0) {
                $null = $builder.Append([char]92, (2 * $backslashRun) + 1)
            }
            else {
                $null = $builder.Append([char]92)
            }
            $null = $builder.Append([char]34)
        }
        else {
            if ($backslashRun -ne 0) {
                $null = $builder.Append([char]92, $backslashRun)
            }
            $null = $builder.Append($character)
        }
        $backslashRun = 0
    }
    if ($backslashRun -ne 0) {
        $null = $builder.Append([char]92, 2 * $backslashRun)
    }
    $null = $builder.Append([char]34)

    $encoded = $builder.ToString()
    if ($encoded.Length -ne $encodedLength) {
        throw 'Windows command-line argument encoding failed its length invariant.'
    }
    return $encoded
}

function Join-OmegaWindowsCommandLineArguments {
    [CmdletBinding()]
    [OutputType([string])]
    param(
        [Parameter(Mandatory = $true, Position = 0)]
        [AllowEmptyCollection()]
        [AllowEmptyString()]
        [string[]]$ArgumentList,

        [ValidateRange(0, 32766)]
        [int]$MaximumLength = 32766
    )

    $builder = [System.Text.StringBuilder]::new()
    for ($index = 0; $index -lt $ArgumentList.Count; ++$index) {
        $encoded = ConvertTo-OmegaWindowsCommandLineArgument `
            -Argument $ArgumentList[$index] `
            -MaximumEncodedLength $MaximumLength
        $separatorLength = if ($index -eq 0) { 0 } else { 1 }
        [long]$nextLength = $builder.Length + $separatorLength + $encoded.Length
        if ($nextLength -gt $MaximumLength) {
            throw 'Windows command line exceeds the bounded launch limit.'
        }
        if ($separatorLength -ne 0) {
            $null = $builder.Append(' ')
        }
        $null = $builder.Append($encoded)
    }
    return $builder.ToString()
}
