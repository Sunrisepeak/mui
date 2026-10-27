# openmui.rhi

The GPU abstraction openmui's renderer is written once on (`omui.rhi`, architecture §8.2, D-33, D-58). It is UI-scale: 2D colour targets, sampled and storage textures, vertex, index and storage buffers, uniforms pushed by value, one blend state per pipeline, and compute. It has no depth buffer, no multisampling and no bindless resources. Backends live in packages of their own (`modules/rhi-sdl` first); this one depends on no graphics library.

```cpp
import openmui.rhi;
using namespace omui::rhi;

auto target = device.create(TextureDesc{.width = 256, .height = 256, .usage = TextureUsage::color_target});
auto pipeline = device.create(GraphicsPipelineDesc{.vertex = &quad_vs, .fragment = &quad_fs, .vertex_buffers = layout});

CommandList frame;
frame.upload(vertices, 0, std::span(quad));
frame.begin_render({*target, LoadOp::clear, {0, 0, 0, 1}});
frame.set_pipeline(*pipeline);
frame.bind_vertex_buffer(0, vertices);
frame.bind_texture(Stage::fragment, 0, image, sampler);
frame.push_uniforms(Stage::fragment, 0, tint);
frame.draw(4);
frame.end_render();
auto done = device.submit(frame);       // validated, then run
auto pixels = device.read(*target, {0, 0, 256, 256});
```

- **Descriptions are checked before a backend sees them** (`check`): sizes and usages, a shader's stage and its code in a format the device takes, vertex attributes inside their stride, read-write storage only in compute shaders.
- **A frame's work is data.** A `CommandList` records uploads, render passes and their draws, compute passes and their dispatches, and copies. The bytes a command carries are copied into the list's arena, so a list of the same shape as the last allocates nothing. Lists hash by content, which is what tests and the trace compare.
- **The device validates every command** (`validate`) against the resources it names before the backend runs anything:
  - passes balanced and not nested, each command in the kind of pass it belongs to;
  - handles live, usages declared, regions and ranges inside their resources;
  - a pipeline set before a draw or a dispatch, its vertex buffers bound, and every slot its shaders read bound.

  A mistake is an `Errc::invalid` naming the command (`command 3 (draw): the fragment shader reads a sampled texture at slot 0, and nothing is bound there`), not a driver crash.
- **Virtual calls are per resource and per submission, never per draw.** A backend implements the protected `make`, `run` and `fetch`, and `describe` for what it created.

Shaders reach a device as `ShaderDesc`: one entry point, its code in each format it was compiled to, and the resources it binds by kind. Slots follow one convention for every backend (SDL_GPU's); [openmui.slang](../slang) compiles Slang to it and checks it.

The tests (`tests/rhi_test.cpp`) run against a device that only remembers what it created: description checks, lists as data, every kind of validation failure, compute passes, readback bounds.
