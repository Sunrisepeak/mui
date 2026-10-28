# openmui.render.effect

A program's own pixel effects, as data and Slang (D-60, D-68; architecture §8). An effect is two files — `<name>.effect.omui.toml`, which names it, says what it does, declares its parameters and how far it reaches outside its layer, and a Slang fragment shader written against the effect template [openmui.render](..) carries — and no C++. The library reads and checks the file (every mistake, where it is, with a suggestion), compiles the shader for the device's format through [openmui.shader](../../shader), refuses a shader that binds more than the template offers, and registers the effect with the renderer's effects: from then on a layer, or any node's `effect` property, names it like a built-in, and the GPU renderer draws it.

```toml
# scanlines.effect.omui.toml
name  = "scanlines"
doc   = "Every period-th row darkened by strength, as on a CRT."
reach = 0                 # logical pixels, or the name of a parameter

[[param]]
name    = "strength"
default = 0.5
min     = 0
max     = 1
doc     = "how much darker a scanline is"
```

```slang
// scanlines.slang
import openmui_effect;

[shader("fragment")]
float4 scanlines_fs(EffectPixel px) : SV_Target {
    const int2 p = at(px);
    const uint4 s = source_texel(p);
    return out8(p.y % 2 == 0 ? scaled(s, 1 - param(0)) : s);
}
```

```cpp
omui::effect::Library fx({.formats = device.caps().formats});
auto loaded = fx.load("fx/scanlines.effect.omui.toml");   // its ref, or every problem
// between frames, on the thread that renders:
for (const auto& r : fx.poll()) if (!r.ok()) for (const auto& d : r.problems) log(d.text());
```

**The template** (`effects/lib/openmui_effect.slang` in openmui.render, carried in the program as text): the source surface, an auxiliary texture, the parameters (`param(i)`), values the renderer derives (`derived(i)`), the surface's size and the device scale, and the CPU reference's helpers — `source_texel`, `source_bilinear`, `q8`, `scaled`, `ramp`, `hash01` (the 64-bit hash, in pairs of 32-bit words), `lround_exact` — so an effect computes what a CPU reference would. A shader that binds anything else (a buffer, a storage texture) is refused: an effect cannot change pipeline state or reach outside its layer's surface.

**Reloading.** `poll()` loads again every effect whose file, shader, or any file the shader imports changed, and replaces the registry's entry in place: its ref stays, its generation grows, and the GPU renderer makes the new pipeline on its next frame. A load names its entry point, so openmui.shader compiles it in one slangc start with every format's run at once: about 170 ms on the Linux CI machine, under the 500 ms M4 asks for (`tests/effect_test.cpp` measures it on every run). A file that no longer loads keeps the effect as it was and reports where it is wrong — slangc's messages become diagnostics with the file, line and column — and is not tried again until it changes again.

**CPU.** A program's effect has a GPU path only; the CPU renderer draws its layer without it and counts it in `effects_skipped` (spec R-3), as it does for an effect it does not know.

Trace category `effect`: a span per load (`effect.load`), a mark per reload (`effect.reload`), the load's milliseconds as a counter (`effect.load_ms`).
