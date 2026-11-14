// One fragment, three outputs, each to its own colour target: the colour as given, its channels turned
// about, and a number worked out from it into a float target (fill's vertex stage and layout).
struct VIn {
    float2 pos : TEXCOORD0;     // NDC
    float4 color : TEXCOORD1;   // premultiplied
    float2 offset : TEXCOORD2;  // per instance
};
struct VOut {
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
};
struct Out {
    float4 first : SV_Target0;
    float4 second : SV_Target1;
    float4 third : SV_Target2;
};

[shader("vertex")]
VOut several_vs(VIn i) {
    VOut o;
    o.pos = float4(i.pos + i.offset, 0, 1);
    o.color = i.color;
    return o;
}

[shader("fragment")]
Out several_fs(VOut i) {
    Out o;
    o.first = i.color;
    o.second = i.color.bgra;
    o.third = float4(i.color.r * 2.0 + i.color.b * 8.0 + 0.25, 0, 0, 1);
    return o;
}
