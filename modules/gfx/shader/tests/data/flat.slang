// What Slang's WGSL gets wrong, from the renderer's own shaders: a `precise`
// variable, and flat varyings whose struct the fragment stage copies.
struct VIn { float2 pos : TEXCOORD0; float4 color : TEXCOORD1; };
struct VOut {
    float4 pos : SV_Position;
    nointerpolation float4 color : TEXCOORD0;
    float2 local : TEXCOORD1;
};

[shader("vertex")]
VOut flat_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos, 0, 1);
    o.color = i.color;
    o.local = i.pos;
    return o;
}

float4 shade(VOut i) {
    precise float k = i.local.x * i.local.y + 0.5;
    return i.color * k;
}

[shader("fragment")]
float4 flat_fs(VOut i) : SV_Target { return shade(i); }
