# omui.theme

Themes as data (view-data-files §3, component-binding §11). A `*.theme.omui.ts` file declares two kinds of thing:

- **Themes**: whole sets of design tokens (light, dark, an application's brand).
- **Overlays**: layers over any theme that change only what they name (a density, reduced motion).

A library composes a theme with overlays into the runtime's `Theme`. Components read only tokens, so the semantic tree is the same under every composition (S2's promise). The conformance scenario `themed` checks this from every language.

```cpp
theme::library().add(json, "brand.theme.omui.json");          // what brand.theme.omui.ts evaluates to
theme::apply(rt, {"dark", "compact", "reduced_motion"});      // or from a script: omui.theme("dark", "compact")
```

## The file

A TypeScript data module (D-139): its default export is one `defineTheme` call, and what it evaluates to, `{ "theme": { … } }`, is the JSON this library reads. `omui-gen embed` evaluates a package's file when it builds; the player evaluates an application's.

```ts
import { defineTheme } from "omui/define";

export default defineTheme({
  light: {
    doc: "Light: slate surfaces, an indigo accent.",
    dark: false,
    colors: {
      accent: "#4f46e5",
      focus: "#6366f1 / 0.55",   // #rgb, #rrggbb, #rrggbbaa, or an exact alpha as CSS writes it
    },
    text: { body: { size: 15, weight: 400, line_height: 1.45, letter_spacing: 0, families: ["Inter"] } },
    spacing: { m: 12 },          // lengths: gap = "m", padding = "xl" in views
    radius: { medium: 8 },
    shadows: { md: { x: 0, y: 4, blur: 12, spread: -2, color: "#0f172a / 0.12" } },
    motion: {                    // the registry's motion tokens, re-mapped
      "motion.fast": { duration: 120, easing: "cubic-bezier(0.2, 0, 0, 1)" },
      "motion.spring.snappy": { stiffness: 400, damping: 32 },
    },
  },
  dark: { extends: "light", dark: true },   // the tokens it changes
  compact: {
    overlay: true,               // a layer over whichever theme it is composed with
    density: 0.8,                // controls' heights, paddings and items, in whole pixels
  },
});
```

- **A theme** is a whole set of tokens, or the tokens it changes in the theme it `extends`.
- **An overlay** extends nothing and does not say whether a theme is dark.
- **Changes are checked.** A theme that extends another, and every overlay, may only change tokens the theme beneath already has, so `backgruond` is a typo (`did you mean background?`), not a new token.
- **Text styles** change field by field: `body: { size: 13 }` keeps the weight.
- **A motion token** is another token's name (`"motion.instant"`), a tween (`duration` in milliseconds, `easing`, `delay`) or a spring (`stiffness`, `damping`, `mass`).

Every mistake in a file is reported in one reading, at its JSON pointer (`/theme/dark/colors/backgruond`) and with the fix where there is one. A file added again replaces what it declared, all or nothing: the player reloads an application's theme files when they are saved, and a broken edit changes nothing. A name declared by another file is refused.

## Where themes come from

- **Theme packages** carry their file embedded and add it with one call:
  - [default](default): light, dark, reduced_motion; the runtime's built-in tokens as data, held equal by its test.
  - [compact](compact): the compact density.
  - [styles](styles): the style packs — glass, neon, retro CRT, ink on paper, liquid; and the platforms' looks: fluent, cupertino, material.
- **A part of a window**: `apply_style(runtime, name, scope)` gives a style to a theme scope only (`Runtime::set_scope`): the nodes whose `theme_scope` names it take the pack, and its cross-fade covers their area. The site shows S2's workshop this way, in the pack chosen, inside a page that keeps its own theme.
- **An application's own files** are listed in its manifest: `themes = ["themes/*.theme.omui.ts"]` and `theme = ["light"]` for the composition to start with.
- **Scripts** choose a composition: `theme("dark", "compact")` in TypeScript.

  `themes()` lists the themes and overlays and the current composition. The C ABI has `omuiTheme` and `omuiThemes`.
