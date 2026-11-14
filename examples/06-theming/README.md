# Example 06 — theming: a theme preview

`theme-preview` shows every common component, the colour tokens and the text styles under a composition you choose:

- **Theme**: light, dark, or the application's own **brand**;
- **Compact**: the density overlay;
- **Reduced motion**: the overlay for people who asked for less motion.

It is written in TypeScript ([`logic/main.ts`](logic/main.ts)), run by the player. The script calls `theme(...)` to compose and `themes()` to show what is composed.

Themes are data (omui.theme):
- openmui's packages give light, dark, compact and reduced_motion;
- [`themes/brand.theme.omui.ts`](themes/brand.theme.omui.ts) is this application's, a teal accent and rounder corners over light.

The manifest lists the brand file (`themes = ["themes/*.theme.omui.ts"]`), and the player reloads it when it is saved. Edit a colour and every component follows at once. A mistake is reported at its place (`/theme/brand/colors/accent`), and the preview stays as it was.

```sh
mcpp run -p omui.example.theming          # then edit examples/06-theming/themes/brand.theme.omui.ts
mcpp run -p omui.example.theming -- --headless --png preview.png
```

The test (`tests/theming_test.cpp`) checks:
- compositions chosen through the controls, as a person or an agent does;
- that the gallery's semantics are identical under light, dark + compact + reduced motion, and brand, while its metrics follow the density (a button 36 pixels high, then 29);
- a saved edit of the brand file, and a broken one;
- the pinned pixels of three compositions.
