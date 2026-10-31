# openmui.shader.rules

The build rule that compiles a package's Slang shaders in its build ([openmui.shader](..)). Each `shaders/<stem>.slang` becomes the module partition `<module>:<stem>_shaders`. It holds an `inline constexpr omui::rhi::ShaderDesc` for every entry point, with its code in each format the build's target takes, in the namespace `<module's namespace>::shaders::<stem>`.

```toml
[build-dependencies]
openmui.shader.rules = { path = "…/modules/gfx/shader/rules", host-module = true }
```

```cpp
// build.mcpp
import mcpp;
import openmui.shader.rules;
int main() { return openmui::shader::rules::shaders({.module = "acme.render"}) ? 0 : 1; }
```

One action per shader runs `omui-shader`, built for the build machine. The action re-runs when the shader, a file it imports (from slangc's depfile) or the tool changes. The formats are the target's (`--target-os`, `--host-os`) unless `options::formats` names them. `includes` and `defines` pass to slangc.
