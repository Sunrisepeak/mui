# openmui.ui.liquid_glass

Liquid glass as components (module `openmui.ui.liquid_glass`, D-64, features-plugins-pipeline §3.3): `liquid_glass`, a container whose surface is a thick transparent material over what lies under it, and `glass_group`, whose members are one piece of glass that merges like drops. Contract-first: `contract/*.contract.omui.toml` are the interfaces; the bases, builders, stubs and schema are generated when the package builds (`openmui.rules`).

```cpp
import openmui.ui.liquid_glass;

omui::register_liquid_glass_components();   // by name, for view documents and scripts
omui::liquid_glass({.id = "toolbar", .label = "Toolbar", .radius = 22, .blur = 4}, {buttons…});
omui::glass_group({.id = "controls", .smoothness = 18}, {
    omui::liquid_glass({.id = "play", .radius = 28}, {play_button}),
    omui::liquid_glass({.id = "seek", .radius = 20}, {seek_slider}),
});
```

- **The look is the renderer's.** A `liquid_glass` pushes the built-in `liquid_glass` effect's layer around itself and its children, its rounded box the layer's shape; the effect reads what the frame holds under it (`OMUI_EXT_backdrop_effect`) and bends it at the bevel (`thickness`, `ior`), splits it by colour (`dispersion`), frosts it (`blur`), tints it (`tint`, `adaptive` — darker over a bright backdrop, lighter over a dark one) and lights the rim facing the light (`light_x`, `light_y`, `rim`). The CPU and GPU renderers draw the same bytes (openmui.render's tests).
- **A group is one piece of glass.** A `glass_group` draws one layer whose shapes are its `liquid_glass` members' laid-out boxes (up to eight), merged smoothly when they come within `smoothness` of each other; its members draw none of their own. Animate a member's place or size with a spring and the drops join and part.
- **Only the look.** Both are ordinary groups in the semantic tree, named by `label`; what an agent or a screen reader sees is the same with glass, without it, and with reduced transparency (`tests/glass_test.cpp`).
- **Reduced transparency** (`Runtime::set_reduce_transparency`, `DesktopOptions::reduce_transparency`, `--reduce-transparency`): every piece of glass is an opaque panel in the theme's `surface` colour, edged in `border`.
