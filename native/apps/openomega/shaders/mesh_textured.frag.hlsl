// In-house textured mesh pixel shader (Gap-A / gameplay Slice 1).
//
// SDL GPU HLSL binding convention (D3D12/DXIL): fragment textures/samplers live
// in register space2. This samples one bound texture using TRIPLANAR mapping
// computed from the interpolated object/world-space position, so it needs no
// per-vertex UVs. The surface normal is derived from screen-space derivatives
// of the object position (valid for the flat-shaded collision geometry we draw
// today); the three axis projections are blended by that normal. A gentle N.L
// term against a fixed key light keeps the geometry readable in 3D.
//
// This is a documented APPROXIMATION: triplanar UVs on the collision geometry
// with a single stand-in level texture. Real per-vertex UVs + per-material
// textures arrive with the VUM visual-geometry decode (Path B), which feeds the
// same pipeline.

Texture2D<float4> t0 : register(t0, space2);
SamplerState s0 : register(s0, space2);

struct VSOutput
{
    float3 ObjectPos : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float4 Position : SV_Position;
};

float4 main(VSOutput input) : SV_Target0
{
    // World-space face normal from derivatives of the object position.
    float3 dpx = ddx(input.ObjectPos);
    float3 dpy = ddy(input.ObjectPos);
    float3 face_normal = normalize(cross(dpx, dpy));

    // Triplanar blend weights from the absolute normal.
    float3 blend = abs(face_normal);
    blend = blend / max(blend.x + blend.y + blend.z, 1e-4f);

    // Texture repeats per world unit. 0.03 gives a legible tiling on level scale.
    const float kUvScale = 0.03f;
    float3 p = input.ObjectPos * kUvScale;
    float4 sample_x = t0.Sample(s0, p.yz);
    float4 sample_y = t0.Sample(s0, p.xz);
    float4 sample_z = t0.Sample(s0, p.xy);
    float4 albedo = sample_x * blend.x + sample_y * blend.y + sample_z * blend.z;

    // Fixed key light so faces at different orientations read distinctly, with a
    // high ambient floor so the textured surfaces stay bright and legible.
    const float3 key_light = normalize(float3(0.4f, 0.9f, 0.3f));
    float lambert = saturate(dot(face_normal, key_light)) * 0.45f + 0.6f;

    return float4(albedo.rgb * lambert, 1.0f);
}
