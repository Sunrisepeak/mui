# hanim.rhi

hanim's prepared shape morphs sampled in batches on a device (convergence plan §6.8 ⑧): the plans `hanim.core` prepares, their two ends uploaded once, every control point of every plan interpolated by one Slang compute dispatch per sample (SPIR-V natively, WGSL in browsers), through `mlibs.rhi` only.

```cpp
import hanim.rhi;
auto batch = hanim::gpu::ShapeBatch::prepare(device, plans);   // the plans' ends uploaded (ready())
(*batch)->record(commands, 0.5, output);                       // output: floats() floats, x then y per point
device.submit(commands);
```

Semantics are `PreparedShapeMorph::sample`'s, the straight line or the batch's one point path (its chord-frame curve), in 32-bit floats: within 1e-4 of the CPU reference on coordinates up to 40. Path noise is refused as unsupported rather than approximated (its lattice hash needs 64-bit integers, which WebGPU lacks): such plans are sampled on the CPU. A refused record appends nothing; recording neither submits nor reads back.

`hanim.rhi.cts` (mlibs.cts, contract `hanim.shape.gpu@1`, level B) compares a thousand plans at five progresses and a curved path against the CPU and checks the refusals; the SDL and WebGPU backends' tests run it on their devices (lavapipe in Linux CI). Trace category `hanim.rhi`: `shape.gpu.prepare`, `shape.gpu.record`.
