// In-house textured mesh vertex shader (Gap-A / gameplay Slice 1).
//
// SDL GPU HLSL binding convention (D3D12/DXIL): a vertex uniform buffer lives in
// register space1; vertex inputs are addressed as TEXCOORDn by attribute
// location (location 0 -> TEXCOORD0, location 1 -> TEXCOORD1, location 2 ->
// TEXCOORD2), matching the mesh pipeline's vertex buffers (slot 0 = float3
// position, slot 1 = float3 per-instance color, slot 2 = float2 per-vertex UV).
// Compiled to DXIL with dxc (see tools/compile_shaders.ps1); OmegaStrain runs
// the SDL GPU D3D12 backend on Windows, so DXIL is what is loaded.
//
// The per-vertex UV is the REAL retail texture coordinate decoded by the VUM
// visual-geometry decoder (source signed16 / 4096), passed straight through to
// the pixel stage. The object-space position is still passed through as well,
// because the pixel shader derives its face normal from that position's screen-
// space derivatives for the readability key light.

cbuffer UBO : register(b0, space1)
{
    float4x4 ModelViewProj;
};

struct VSInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float2 Uv : TEXCOORD2;
};

struct VSOutput
{
    float3 ObjectPos : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float2 Uv : TEXCOORD2;
    float4 Position : SV_Position;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.ObjectPos = input.Position;
    output.Color = input.Color;
    output.Uv = input.Uv;
    output.Position = mul(ModelViewProj, float4(input.Position, 1.0f));
    return output;
}
