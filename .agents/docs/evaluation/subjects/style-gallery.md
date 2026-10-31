# Subject: style-gallery — style packs and the style workshop

**What:** `showcase/style-gallery`: a real settings panel and dashboard drawn in five style packs (glass, ink, liquid, neon, retro) with Slang effects and cross-fades, and the workshop (studio) that edits a pack and exports it (`*.style.omui.toml`); the theme packs in `modules/ui/theme`. Read its README, dev-story.md, the features-plugins design (liquid glass), src/, tests/.

**Top 3:** Apple's Liquid Glass (system-wide material) · Material Theme Builder · tweakcn (the shadcn/ui theme editor). **Reference:** Figma Variables, Tokens Studio.

**Dimensions:**
- `pack_glass`, `pack_ink`, `pack_liquid`, `pack_neon`, `pack_retro` — each pack's fidelity to its style and its craft (edges, glow, grain, refraction, banding, readability).
- `layout_composition` — grid, spacing, alignment, use of space in the gallery and the workshop.
- `typography` — sizes, weights, labels, values, units.
- `contrast_accessibility` — contrast in every pack, focus, disabled states, reduced transparency.
- `effects_quality` — shader effects' quality and cost; transitions between packs.
- `workshop_usability` — editing a pack: parameter names, values, undo/reset, preview, import/export, feedback.
- `semantic_sameness` — the same semantic tree in every pack, proven.
- `framework_showcase` — effects as plugins, theme scopes, agents switching packs.
- `code_tests_docs` — code, tests, README, dev story, signature shot.

**Run:**
- `omui-styles` (openmui.showcase.style_gallery)

**Use:**
1. Switch every pack (glass, ink, liquid, neon, retro) and watch the cross-fade.
2. In each pack, operate the settings panel and dashboard with clicks, drags and keys: every switch, slider, field, tab, menu answers and reads.
3. The workshop: change a pack's values (colour, radius, blur, glow …), see them applied, undo and reset, compare packs.
4. Contrast and legibility in each pack, light and dark.
5. As an agent: set a pack's value through the tree and check the picture follows.

**Images:** each pack full-size, the README picture.
