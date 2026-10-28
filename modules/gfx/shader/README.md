# openmui.shader

Slang shaders to what an [omui.rhi](../rhi) device takes (architecture §8.3, D-34). Every entry point of a `.slang` file is compiled by slangc to each format a target's backends take, its resources are checked against rhi's binding convention, and the result is written as a C++ module partition of `rhi::ShaderDesc` constants. The same compile works in memory, which is how a shader edited while a program runs is compiled again.

```sh
omui-shader shaders/quad.slang --out gen/quad_shaders.cppm \
    --module acme.render --partition quad_shaders --namespace acme::render::shaders::quad \
    --target-os windows --host-os windows        # SPIR-V and DXBC
```

```cpp
import acme.render;   // exports :quad_shaders
auto p = device.create(GraphicsPipelineDesc{.vertex = &acme::render::shaders::quad::quad_vs,
                                            .fragment = &acme::render::shaders::quad::quad_fs, …});
```

- **Formats per target** (`formats_for`): SPIR-V for Vulkan, MSL on Apple, and DXBC for D3D12 when the host is Windows (its `d3dcompiler` compiles it). `--formats` names others (`dxil` with DXC present).
- **The binding convention** (SDL_GPU's), written `[[vk::binding(index, set)]]` with the matching `register(…, space<set>)`:

  | stage | read-only resources | read-write resources | constant buffers |
  |---|---|---|---|
  | vertex | set 0 | — | set 1 |
  | fragment | set 2 | — | set 3 |
  | compute | set 0 | set 1 | set 2 |

  Read-only resources are sampled textures (`Sampler2D`, a texture with its sampler), then storage textures, then storage buffers. Read-write ones are storage textures, then storage buffers. Each kind takes the indices right after the kind before it, from 0, so index *i* of a stage's samplers is rhi's slot *i*.
- **What each entry point uses** is read from its own SPIR-V, since Slang's reflection lists every global for every entry point. A binding off the convention is refused with where it should be, as the author writes it:

  ```
  wrong_set.slang (fragment, the fragment entry point `paint`): `image` is a sampled texture at set 0, binding 0;
  as the stage's read-only resources it should be [[vk::binding(0, 2)]] … register(t0, space2) : register(s0, space2)
  ```

  A Vulkan binding and an HLSL register that disagree are refused the same way. So are separate `SamplerState`s, resource arrays, loose global uniforms, and an entry point named `main` (Metal reserves it).
- **The output is deterministic** for the same program, and the depfile names every file the shader read.
- **slangc is mcpp's to provision** (`[xlings.workspace] "xim:slang"`). The build program writes its path into the library as `provisioned_slangc`, so a build or a test needs nothing on `PATH`. `OMUI_SLANGC` overrides it.
- **Compiled in the process when it can be** (`Options::in_process`, on by default): Slang's library beside that slangc (`libslang-compiler`) is loaded once and given slangc's own arguments, its sessions kept. Most of a slangc run is starting the program and loading Slang's core module (130 of 160 ms on a desktop, about three times that on a CI Mac), so a warm compile of SPIR-V and MSL takes about 20 ms and a reloaded effect about 50. The files written, the reflection, the dependencies and the diagnostics are slangc's (a test compiles both ways and compares). Without the library, or with `OMUI_SLANGC_PROCESS=1`, slangc runs.

The tests (`tests/slang_test.cpp`) compile real shaders with the provisioned slangc: a quad's vertex and fragment shaders in SPIR-V and MSL, a compute blur, every kind of refusal, the emitted C++, and the formats per target.

## Inside

- [rules](rules) (`openmui.shader.rules`): the build rule that compiles a package's `shaders/*.slang` at build time.
