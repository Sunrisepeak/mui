// particles: the layer's pixels as square particles, one instance a cell in
// the reference's order (rows of cells, then cells), each moved by its whole
// pixel offset and faded, drawn over the ones before it.
// derived(0…7): size, the cells' area x0, y0, x1, y1, stagger, spread, gravity
// (device pixels); params: progress, seed.
import openmui_effect;

struct Particle {
    float4 pos : SV_Position;
    nointerpolation int4 cell : TEXCOORD0;   // the cell in the source: x0, y0, x1, y1
    nointerpolation int2 move : TEXCOORD1;   // its offset, whole pixels
    nointerpolation float alpha : TEXCOORD2;
};

[[vk::binding(0, 1)]] ConstantBuffer<EffectFrame> vframe : register(b0, space1);

float vderived(int i) { return vframe.derived[i >> 2][i & 3]; }
float vparam(int i) { return vframe.params[i >> 2][i & 3]; }

[shader("vertex")]
Particle particles_vs(uint vertex : SV_VertexID, uint instance : SV_InstanceID) {
    const int size = int(vderived(0));
    const int x0 = int(vderived(1)), y0 = int(vderived(2)), x1 = int(vderived(3)), y1 = int(vderived(4));
    const float stagger = vderived(5), spread = vderived(6), gravity = vderived(7);
    const int across = (x1 - x0 + size - 1) / size;
    const int i = int(instance) % across, j = int(instance) / across;
    const int cx = x0 + i * size, cy = y0 + j * size;
    const float progress = vparam(0), seed = vparam(1);
    const float delay = hash01(seed, i, j, 1) * stagger;
    const float q = clamp01((progress - delay) / (1 - stagger));
    Particle o;
    o.cell = int4(cx, cy, min(cx + size, x1), min(cy + size, y1));
    o.alpha = 1 - q;
    precise const float e = q * (2 - q);
    precise const float dx = (hash01(seed, i, j, 2) * 2 - 1) * spread * e;
    precise const float fall = gravity * q * q;
    precise const float dy = (hash01(seed, i, j, 3) * 2 - 1) * spread * e + fall;
    o.move = int2(lround_exact(dx), lround_exact(dy));
    const uint corners[6] = { 0, 1, 2, 1, 3, 2 };
    const uint c = corners[vertex % 6];
    float2 device = float2((c & 1) != 0 ? o.cell.z : o.cell.x, (c >> 1) != 0 ? o.cell.w : o.cell.y) + float2(o.move);
    if (q >= 1) device = float2(-1, -1);  // gone: nothing to draw
    const float2 n = vframe.size.xy;
    o.pos = float4(device.x / n.x * 2 - 1, 1 - device.y / n.y * 2, 0, 1);
    return o;
}

[shader("fragment")]
float4 particles_fs(Particle p) : SV_Target {
    const int2 from = int2(p.pos.xy) - p.move;
    const uint4 s = source_texel(from);
    if (s.a == 0) discard;
    return out8(scaled(s, p.alpha));
}
