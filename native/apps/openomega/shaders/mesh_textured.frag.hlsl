// In-house textured mesh pixel shader (Gap-A / gameplay Slice 1).
//
// SDL GPU HLSL binding convention (D3D12/DXIL): fragment textures/samplers live
// in register space2. This samples one bound texture with the mesh's REAL
// per-vertex texture coordinates, decoded from the retail VUM visual geometry
// (source signed16 / 4096) and delivered on vertex attribute location 2. The
// fabricated triplanar projection that used to stand in for them here is gone.
//
// The decoded UVs span well beyond [0, 1] (roughly +/-8), which is deliberate
// retail tiling, so the sampler must stay REPEAT on every axis -- do not clamp.
//
// Still an approximation, and deliberately out of this slice's scope: ONE
// stand-in level texture is bound for every environment mesh. Per-material
// texture binding and name->TDX resolution are RE-blocked follow-ups (Path B).
//
// The surface normal is still derived from screen-space derivatives of the
// object position, and feeds only the gentle N.L readability key light below --
// it no longer selects any texture projection.

Texture2D<float4> t0 : register(t0, space2);
SamplerState s0 : register(s0, space2);

struct VSOutput
{
    float3 ObjectPos : TEXCOORD0;
    float3 Color : TEXCOORD1;
    float2 Uv : TEXCOORD2;
    float4 Position : SV_Position;
};

float4 main(VSOutput input) : SV_Target0
{
    // World-space face normal from derivatives of the object position.
    float3 dpx = ddx(input.ObjectPos);
    float3 dpy = ddy(input.ObjectPos);
    float3 face_normal = normalize(cross(dpx, dpy));

    // The real decoded texture coordinates, interpolated across the triangle.
    float4 albedo = t0.Sample(s0, input.Uv);

    // Fixed key light so faces at different orientations read distinctly, with a
    // high ambient floor so the textured surfaces stay bright and legible.
    const float3 key_light = normalize(float3(0.4f, 0.9f, 0.3f));
    float lambert = saturate(dot(face_normal, key_light)) * 0.45f + 0.6f;

    return float4(albedo.rgb * lambert, 1.0f);
}
