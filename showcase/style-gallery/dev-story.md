# Dev story — the style workshop

How `omui-styles` was made: one real interface (a settings panel and a dashboard) shown in each of openmui's five style packs, with an editor for the pack shown and an export another program loads.

![The five style packs](media/five-packs.png)

## 1. What we set out to make

A tool a designer would actually use to make a style pack, and, in using it, the proof that in openmui a style is only pixels. The semantic tree and an agent's actions are the same under every pack.

## 2. The layers

| Layer | Package | What it is |
|---|---|---|
| Theme engine | `openmui.theme` | themes, overlays and style packs as data (`*.style.omui.toml`), composed; `apply_style` with a cross-fade |
| The five packs | `openmui.theme.styles` | glass, neon, retro, ink, liquid: data embedded in the program |
| Effects | `openmui.render.effect`, Slang shaders | `liquid_glass`, `neon`, `wobble`, `metaball`, and the window's `crt` and `paper` |
| Components | `openmui.ui.base` | each says which surfaces it paints (`effect_shapes`: a switch's track and thumb), so a pack's effect reaches the right shapes |
| The application | `showcase/style-gallery` | the preview, the sidebar, the editor, the export, the verbs `styles.show` and `styles.export` |

A pack never names an application's widget. It targets component types (`button`), roles (`role:region`) and the window, so the same pack works for any program.

## 3. How each part was built

- **Packs are data.** A pack is the theme it starts from, the tokens it changes, and the effects it puts on types and roles. The five ship embedded, and an exported pack is a `*.style.omui.toml` that reads back as itself.
- **Switching is a cross-fade** from the frame before (`Runtime::crossfade`); editing applies at once, with no fade.
- **Agents** switch packs with `styles.show`. Exporting is `styles.export`, an external effect for which the agent must state its answer (S-1), because it writes a file.
- **Tests** pin the pixels of all five packs on every platform and assert one semantic tree under all of them.

## 4. Performance

Effects run on the GPU path (`omui.rhi`) and in the CPU reference renderer, which the headless tests pin. A pack switch costs one cross-fade texture.

## 5. Self-review

- **A cross-fade changes every pixel of every frame.** It is too heavy for a README loop: 3.8 MB as an animated PNG, so the loop shows the packs held still instead. Delta-coded animated PNG is right for UI loops (a ball over a still field: 144 KB) and wrong for full-frame transitions. If the showcases need such loops, a royalty-free video format belongs in the media tools (openmui-video, S5), not a bigger PNG.
- **The package is named `styles` in a directory named `style-gallery`** (and the Markdown wall is `wall` in `markdown-wall`). D-80 says the path is the package's name. The lint rule `layout` only covers libraries under `modules/` and `components/`, so it did not catch this. Either the directories or the packages should be renamed, and `layout` should cover showcases.
- **Editing a pack's effects is numbers in fields.** For a designer, the effect parameters want direct manipulation on the preview: drag the glass's thickness on the panel itself. That is a general editing capability (canvas-kit's handles over a live component) rather than something this tool should hand-build.

## 6. Practices that generalise

See [best practices](../../docs/en/best-practices.md): styles target types and roles, never an application's widgets; components declare the surfaces they paint; a style is proven to be only pixels by asserting the semantic tree, not by looking.
