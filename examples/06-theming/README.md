# Example 06 — theming: a theme preview

`theme-preview` shows every common component, the colour tokens and the text styles under a composition you choose:

- **Theme**: light, dark, or the application's own **brand**;
- **Compact**: the density overlay;
- **Reduced motion**: the overlay for people who asked for less motion.

It is written in Lua ([`logic/main.lua`](logic/main.lua)), run by the player. The script calls `omui.theme(...)` to compose and `omui.themes()` to show what is composed.

Themes are data (openmui.theme):
- openmui's packages give light, dark, compact and reduced_motion;
- [`themes/brand.theme.omui.toml`](themes/brand.theme.omui.toml) is this application's, a teal accent and rounder corners over light.

The manifest lists the brand file (`themes = ["themes/*.theme.omui.toml"]`), and the player reloads it when it is saved. Edit a colour and every component follows at once. A mistake is reported with its line and column, and the preview stays as it was.

```sh
mcpp run -p openmui.example.theming          # then edit examples/06-theming/themes/brand.theme.omui.toml
mcpp run -p openmui.example.theming -- --headless --png preview.png
```

The test (`tests/theming_test.cpp`) checks:
- compositions chosen through the controls, as a person or an agent does;
- that the gallery's semantics are identical under light, dark + compact + reduced motion, and brand, while its metrics follow the density (a button 36 pixels high, then 29);
- a saved edit of the brand file, and a broken one;
- the pinned pixels of three compositions.
