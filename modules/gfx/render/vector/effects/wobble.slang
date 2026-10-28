// wobble: drawn by hand — the layer pushed about by a smooth noise and inked
// at its edges. effects.cpp's wobble, operation for operation.
// params: amount, size, seed, ink.
import openmui_effect;

// Value noise in [-1, 1): hash01 at a cell's corners, smoothly blended.
float smooth_noise(float seed, float x, float y, uint salt) {
    const float fx = floor(x), fy = floor(y);
    const int ix = int(fx), iy = int(fy);
    const float tx = x - fx, ty = y - fy;
    precise const float sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
    const float a = hash01(seed, ix, iy, salt), b = hash01(seed, ix + 1, iy, salt);
    const float c = hash01(seed, ix, iy + 1, salt), d = hash01(seed, ix + 1, iy + 1, salt);
    precise const float top = a + (b - a) * sx, bottom = c + (d - c) * sx;
    precise const float v = (top + (bottom - top) * sy) * 2 - 1;
    return v;
}

[shader("fragment")]
float4 wobble_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const float scale = device_scale();
    precise const float amount = param(0) * scale;
    const float size = max(1.0, param(1) * scale), seed = param(2), ink = clamp01(param(3));
    const float qx = float(p.x) + 0.5, qy = float(p.y) + 0.5;
    precise const float ox = smooth_noise(seed, qx / size, qy / size, 1) * amount, oy = smooth_noise(seed, qx / size, qy / size, 2) * amount;
    precise const float sx = qx - ox, sy = qy - oy;
    const float4 c = source_bilinear(float2(sx, sy));
    const float4 l = source_bilinear(float2(sx - 1, sy)), r = source_bilinear(float2(sx + 1, sy));
    const float4 u = source_bilinear(float2(sx, sy - 1)), d = source_bilinear(float2(sx, sy + 1));
    precise const float grad = abs(r.a - l.a) + abs(d.a - u.a);
    const float e = clamp01(grad / 255.0);
    precise const float k = 1 - ink * 0.7 * e;
    precise const float4 o = float4(c.r * k, c.g * k, c.b * k, c.a);
    return out8(q8(o));
}
