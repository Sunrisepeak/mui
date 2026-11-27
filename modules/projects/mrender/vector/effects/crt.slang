// crt: an old screen over the layer — bent outward, lit in scanlines (the
// aux texture holds each row's factor, computed on the CPU as the reference
// computes it), through an aperture grille, darker towards its corners;
// black beyond the glass. effects.cpp's crt, operation for operation.
// params: curvature, scanlines, mask, vignette.
import openmui_effect;

[shader("fragment")]
float4 crt_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const float curve = clamp(param(0), 0.0, 0.5), mask = clamp01(param(2)), vignette = clamp01(param(3));
    const float hw = max(effect.layer.z * 0.5, 1.0), hh = max(effect.layer.w * 0.5, 1.0);
    precise const float cx = effect.layer.x + hw, cy = effect.layer.y + hh;
    const float scan = effect_aux.SampleLevel(float2((float(p.y) + 0.5) / float(surface_size().y), 0.5), 0).r;
    const float qx = float(p.x) + 0.5, qy = float(p.y) + 0.5;
    const float u = (qx - cx) / hw, v = (qy - cy) / hh;
    precise const float r2 = u * u + v * v;
    precise const float f = 1 + curve * r2;
    precise const float su = u * f, sv = v * f;
    if (abs(su) > 1 || abs(sv) > 1) return out8(uint4(0, 0, 0, 255));
    precise const float sx = cx + su * hw, sy = cy + sv * hh;
    const float4 c = source_bilinear(float2(sx, sy));
    const int col = p.x % 3;
    const float m = 1 - mask * 0.5;
    const float mr = col == 0 ? 1.0 : m, mg = col == 1 ? 1.0 : m, mb = col == 2 ? 1.0 : m;
    precise const float vig = 1 - vignette * 0.5 * r2;
    const float k = scan * max(vig, 0.0);
    precise const float4 o = float4(c.r * mr * k, c.g * mg * k, c.b * mb * k, c.a);
    return out8(q8(o));
}
