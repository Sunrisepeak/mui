// Vulkan's draw parameters, an instance's index, and a variable GLSL would
// reserve once Slang names it (`gl` → gl_0): what ESSL has instead.
struct VOut { float4 pos : SV_Position; nointerpolation uint corner : TEXCOORD0; };

[shader("vertex")]
VOut drawn_vs(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const float gl = sqrt(float(vertex % 3) + float(instance));
    VOut o;
    o.pos = float4(gl > 0 ? 1 / gl : 0.0, gl * gl, 0.5, 1);
    o.corner = vertex % 6;
    return o;
}

[shader("fragment")]
uint drawn_fs(VOut i) : SV_Target { return i.corner; }
