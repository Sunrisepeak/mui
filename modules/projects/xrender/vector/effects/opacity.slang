// The shared effect template scales all premultiplied channels together,
// with the same endpoint bypasses and byte rounding as the CPU reference.
import openmui_effect;

[shader("fragment")]
float4 opacity_fs(EffectPixel px) : SV_Target {
    return out8(scaled(source_texel(at(px)), param(0)));
}
