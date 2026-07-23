// In-house textured mesh vertex shader (Gap-A / gameplay Slice 1).
//
// SDL GPU HLSL binding convention (D3D12/DXIL): a vertex uniform buffer lives in
// register space1; vertex inputs are addressed as TEXCOORDn by attribute
// location (location 0 -> TEXCOORD0, location 1 -> TEXCOORD1), matching the
// existing mesh pipeline's vertex buffers (slot 0 = float3 position, slot 1 =
// float3 per-instance color). Compiled to DXIL with dxc (see
// tools/compile_shaders.ps1); OmegaStrain runs the SDL GPU D3D12 backend on
// Windows, so DXIL is what is loaded.
//
// The object-space position is passed through to the pixel stage so the pixel
// shader can compute triplanar texture coordinates from it (no per-vertex UV
// attribute is required for this slice; real per-vertex UVs arrive with the VUM
// visual-geometry decode, Path B).

cbuffer UBO : register(b0, space1)
{
    float4x4 ModelViewProj;
};

struct VSInput
{
    float3 Position : TEXCOORD0;
    float3 Color : TEXCOORD1;
};

struct VSOutput
{
    float3 ObjectPos : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float4 Position : SV_Position;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    output.ObjectPos = input.Position;
    output.Color = input.Color;
    output.Position = mul(ModelViewProj, float4(input.Position, 1.0f));
    return output;
}
