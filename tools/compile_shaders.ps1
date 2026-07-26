# Regenerate the embedded DXIL headers for the in-house mesh shaders.
#
# OmegaStrain runs the SDL GPU D3D12 backend on Windows, which loads DXIL. The
# committed *.dxil.h headers are the build inputs (SdlGpuHost #includes them), so
# CI/builds do NOT need a shader compiler. Run this only after editing the .hlsl
# sources, then commit the regenerated headers.
#
# dxc ships with the Windows SDK. This build's dxc lacks SPIR-V codegen, so only
# DXIL is emitted (sufficient for Windows/D3D12); SPIRV/MSL for other backends is
# a follow-up (SDL_shadercross or a SPIRV-enabled dxc).
#
# Usage:  powershell -NoProfile -File tools/compile_shaders.ps1

$ErrorActionPreference = 'Stop'
$shaderSourceDir = Join-Path $PSScriptRoot '..\shaders\openomega'
$shaderHeaderDir = Join-Path $PSScriptRoot '..\native\apps\openomega\sdl_shaders'
New-Item -ItemType Directory -Force -Path $shaderHeaderDir | Out-Null

# Locate dxc: prefer the Windows Kits bin, else PATH.
$dxc = $null
$kits = Get-ChildItem 'C:\Program Files (x86)\Windows Kits\10\bin' -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending
foreach ($k in $kits) {
    $cand = Join-Path $k.FullName 'x64\dxc.exe'
    if (Test-Path $cand) { $dxc = $cand; break }
}
if (-not $dxc) { $dxc = (Get-Command dxc.exe -ErrorAction SilentlyContinue).Source }
if (-not $dxc) { throw 'dxc.exe not found (install the Windows SDK or put dxc on PATH)' }
Write-Host "Using dxc: $dxc"

function Convert-BinToHeader($binPath, $headerPath, $name) {
    $bytes = [System.IO.File]::ReadAllBytes($binPath)
    $sb = New-Object System.Text.StringBuilder
    [void]$sb.Append("static const unsigned char $name`[] = {`n")
    for ($i = 0; $i -lt $bytes.Length; $i += 12) {
        $chunk = $bytes[$i..([Math]::Min($i + 11, $bytes.Length - 1))]
        $hex = ($chunk | ForEach-Object { '0x{0:x2}' -f $_ }) -join ', '
        $comma = if ($i + 12 -lt $bytes.Length) { ',' } else { '' }
        [void]$sb.Append("  $hex$comma`n")
    }
    [void]$sb.Append("};`n")
    [void]$sb.Append("static const unsigned int ${name}_len = $($bytes.Length);`n")
    [System.IO.File]::WriteAllText($headerPath, $sb.ToString())
    Write-Host "wrote $headerPath ($($bytes.Length) bytes)"
}

$stages = @(
    @{ src = 'mesh_textured.vert.hlsl'; profile = 'vs_6_0'; name = 'mesh_textured_vert_dxil'; header = 'mesh_textured.vert.dxil.h' },
    @{ src = 'mesh_textured.frag.hlsl'; profile = 'ps_6_0'; name = 'mesh_textured_frag_dxil'; header = 'mesh_textured.frag.dxil.h' }
)
foreach ($s in $stages) {
    $src = Join-Path $shaderSourceDir $s.src
    $tmp = [System.IO.Path]::GetTempFileName() + '.dxil'
    & $dxc -T $s.profile -E main $src -Fo $tmp
    if ($LASTEXITCODE -ne 0) { throw "dxc failed for $($s.src)" }
    Convert-BinToHeader $tmp (Join-Path $shaderHeaderDir $s.header) $s.name
    Remove-Item $tmp -ErrorAction SilentlyContinue
}
Write-Host 'Done. Commit the regenerated *.dxil.h headers.'
