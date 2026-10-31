# Research: Apple's Liquid Glass, where openmui stands, and a plan

Written by codex (with web search) after the maintainer found the glass pack "glass, not liquid glass" — the material, its motion, components and their overlap.

**openmui has several of the right optical primitives, but its shipped packs do not yet form a Liquid Glass design system.** The maintainer’s assessment is accurate at the experience level: the workshop presents frosted content panels and a separate fluid slider decoration. The missing work connects material, interaction, component geometry, transitions, and overlapping surfaces.

I inspected the current working tree, the requested workshop captures, and additional existing renderer/group captures. No repository files were changed or new benchmarks run. The supplied [Medium article](https://medium.com/@kjkartikjt0002/liquid-glass-ios-26-full-technical-analysis-53768327a062) could not be retrieved; searches did not recover its contents. I read the [Reddit analysis](https://www.reddit.com/r/LiquidGlassDesign/comments/1l9f3ud/apples_liquid_glass_design_a_comparative_analysis/), which usefully emphasizes refraction and responsive motion, but mixes observation with hardware and product speculation. Apple’s sessions, guidelines, and API documentation provide the technical baseline below.

**1. What Apple’s Liquid Glass actually is**

Liquid Glass is a coordinated material and interaction system. Its defining optical behavior is **lensing**: content underneath changes apparent position and shape, particularly near curved edges. Blur scatters detail; refraction relocates it. A credible implementation therefore needs visible displacement against a sharp test background, even with blur disabled. Specular highlights establish surface curvature and move with lighting or interaction. Apple explicitly describes refraction from below, reflected light from the surroundings, and responsive edge lensing. [WWDC25: Platforms State of the Union](https://developer.apple.com/videos/play/wwdc2025/102/)

Thickness is perceptual rather than a literal glass slab measurement. Edge curvature, displacement, highlights, scattering, and shadow jointly establish depth. Increasing blur alone produces a thicker-looking fog. Subtle chromatic separation can enrich refractive edges, but the reviewed Apple sources publish neither a dispersion coefficient nor a requirement for conspicuous rainbow fringes. They also provide no universal index of refraction, bevel width, or optical equation.

There are two material variants:

- **Regular** provides background blur and luminosity adjustment for readable controls, navigation, and text-heavy surfaces.
- **Clear** prioritizes media visibility and remains substantially more transparent. It belongs over visually rich content. Apple recommends considering a **35% black dimming layer** when that content is bright; sufficiently dark content may need none.

These are semantic choices, not simply “blur on” and “blur off.” [Apple HIG: Materials](https://developer.apple.com/design/human-interface-guidelines/materials)

Adaptation includes the foreground. Small navigation surfaces can change between light and dark appearance according to underlying content, with symbols and labels changing polarity. Larger surfaces such as sidebars become more opaque to preserve readability. Tint behaves like colored glass influenced by the background, rather than an opaque brand-color fill. Color should emphasize selected functions or primary actions. [Apple HIG: Color](https://developer.apple.com/design/human-interface-guidelines/color)

Motion contributes equally to material identity. Touch immediately flexes and illuminates the material; its response is gel-like. Menus grow from their triggering controls, and larger presentations acquire more substantial optical treatment. Shadows also respond to the background, becoming stronger where separation is needed. These are coordinated responses, not independent decorative animations. [WWDC25: Meet Liquid Glass](https://developer.apple.com/videos/play/wwdc2025/219/)

The SwiftUI APIs expose important distinctions:

- [`glassEffect(_:in:)`](https://developer.apple.com/documentation/swiftui/view/glasseffect(_:in:)) applies material behind a view and foreground effects over it; its defaults are regular glass and a capsule.
- [`GlassEffectContainer`](https://developer.apple.com/documentation/swiftui/glasseffectcontainer) collects multiple shapes for joint rendering and interaction. Larger container spacing causes approaching shapes to blend sooner.
- [`glassEffectID(_:in:)`](https://developer.apple.com/documentation/swiftui/view/glasseffectid(_:in:)) supplies identity for transitions.
- Explicit unions combine compatible contributions into one material shape. Identity, proximity, and union membership are separate concepts. Apple’s examples use **20- and 40-point spacing**; these are examples, not universal design constants. [Applying Liquid Glass to custom views](https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views)

Glass belongs principally to the **functional navigation layer above content**. Apple advises against glass on glass: controls inside a glass surface should use lightweight fills and vibrancy rather than another complete glass treatment. [Meet Liquid Glass](https://developer.apple.com/videos/play/wwdc2025/219/)

Overlapping or adjacent material participants therefore require deliberate grouping. Apple explains that glass cannot sample other glass; a container provides a shared sampling region for consistent rendering. Other contextual transitions include an opt-in tab bar that minimizes on downward scrolling and expands on reverse scrolling. Partial-height sheets use glass, becoming opaque as they expand to full height. [WWDC25: Build a SwiftUI app with the new design](https://developer.apple.com/videos/play/wwdc2025/323/)

Scroll-edge effects are another mechanism: content softens beneath floating controls. The default soft treatment transitions gradually; the hard treatment provides stronger separation for dense controls or pinned headers. Apple advises one effect per view, avoiding stacked treatments. Geometry also matters: capsules use half-height radii, while nested concentric corners derive inner radii from outer radii and padding. [WWDC25: Get to know the new design system](https://developer.apple.com/videos/play/wwdc2025/356/)

States must remain distinguishable. Pressed interaction produces responsive material feedback; selection conveys persistent meaning; keyboard focus needs a readable location indicator; disabled controls must communicate unavailability. Apple documents interactive touch/pointer reactions, but does not publish a complete universal table of state opacities and spring constants. Touch motion is stronger than trackpad motion. [Custom glass](https://developer.apple.com/documentation/swiftui/applying-liquid-glass-to-custom-views), [HIG: Motion](https://developer.apple.com/design/human-interface-guidelines/motion)

Accessibility modifies the material system: Reduce Transparency obscures more background; Increase Contrast favors strong black/white separation and contrasting borders; Reduce Motion suppresses elasticity and reduces effect intensity. These settings must apply to custom material behavior too. [Meet Liquid Glass](https://developer.apple.com/videos/play/wwdc2025/219/)

**2. Where openmui stands**

The existing implementation deserves more credit than “only blur,” while the shipped presentation deserves less credit than “Liquid Glass.”

In [effects.cpp](/home/speak/workspace/mui/modules/gfx/render/vector/src/effects.cpp:321) and [liquid_glass.slang](/home/speak/workspace/mui/modules/gfx/render/vector/effects/liquid_glass.slang), openmui already computes rounded-shape signed distances, distance gradients, edge displacement, separate RGB sampling, tint, luminance adjustment, directional rim lighting, and a fine inner edge. CPU and GPU implementations follow corresponding operations.

The current displacement is approximately:

`offset = (ior − 1) × thickness × (1 − edge_depth/thickness)²`

At the glass pack’s 14-pixel bevel and default index 1.45, maximum displacement is about **6.3 logical pixels**. This is real refraction. However, “thickness” controls bevel width and displacement together; there is no independent surface height profile. The normalized distance gradient supplies an outward 2D direction, rather than a 3D normal derived from surface slope. The result is a useful stylized lens, with limited control over its cross-section.

The lighting is simpler still. A directional facing term brightens the rim, and a narrow line brightens the inner edge. There is no proper specular lobe, Fresnel response, spatial light source, contact illumination, or automatic coupling to motion. Configurable `light_x` and `light_y` provide useful inputs, but the packs leave them static. Adaptive shadow and surrounding-color spill are absent from this effect.

Dispersion exists. The pack’s value **0.4** changes red/blue displacement by ±20% around green. But the entire backdrop is first frosted, weakening the very edge detail that would establish lensing.

Adaptation is currently a local brightness correction applied **after tinting**, capped at an 8% darkening/lightening adjustment. It neither selects foreground polarity nor evaluates contrast, background detail, or surface size. Labels remain theme-colored. Calling this sufficient to “keep content legible” overstates its guarantee.

Backdrop plumbing is sound as a starting point. `EffectInput` includes source pixels, parameters, device scale, layer bounds, backdrop pixels, and shapes. The CPU captures the current destination when drawing reaches the layer; the GPU copies the immediate parent target into an auxiliary texture. Both can apply three horizontal/vertical box-blur pairs. This establishes actual backdrop access, rather than a wallpaper approximation.

Its scope matters: the backdrop is the **immediate rendering destination**, which may itself be an offscreen layer. Independently rendered sibling glass can sample previously composited glass; nested glass can see an incomplete offscreen backdrop. These behaviors need explicit material-plane rules.

The workshop’s [glass pack](/home/speak/workspace/mui/modules/ui/theme/styles/styles/glass.style.omui.toml) applies `liquid_glass` to **every `role:region`**. That coats Settings, statistics, and chart cards—content surfaces. It combines blur 10, white tint 12%, a translucent white panel background, borders, and rim strength 0.7. The additional panel fill further veils the backdrop.

The reviewed [glass capture](/tmp/openmui-tests/styles-glass/glass.png) consequently reads as lavender frosted cards with bright edges. Large flat wallpaper discs provide little fine detail to reveal refraction. This is a coherent frosted style, but its material placement, static highlights, and ordinary controls do not establish Apple’s interaction language.

The [liquid pack](/home/speak/workspace/mui/modules/ui/theme/styles/styles/liquid.style.omui.toml) is a different treatment: a light palette and `metaball` on sliders, with smoothness 12 and accent-colored bridges. [metaball.slang](/home/speak/workspace/mui/modules/gfx/render/vector/effects/metaball.slang) performs a smooth shape union and fills it with color; it reads no backdrop. The [liquid capture](/tmp/openmui-tests/styles-liquid/liquid.png) shows a fluid-looking connection around each slider thumb, while the panels remain opaque white. It demonstrates shape blending, not glass optics.

There is already a valuable [glass component package](/home/speak/workspace/mui/components/liquid-glass/src/glass.cpp). `glass_group` renders direct glass children through one shared effect, suppressing individual member effects. It can merge and separate their shapes as layout changes. Existing captures confirm visible bridges.

Its limitations are substantial:

- Eight shapes maximum; additional members are silently omitted.
- Membership is inferred from properties, not explicit material participation.
- Shapes use public layout bounds, which omit visual transforms.
- No identity-based appearance/disappearance history or retained exit geometry.
- No cross-overlay transition coordination or material state machine.
- Per-paint vectors conflict with the intended allocation-free frame path.

The generic [frame pipeline](/home/speak/workspace/mui/modules/ui/view/src/frame.cpp:531) wraps a styled node’s **whole subtree** in one effect layer. Its heuristic shapes include the node and immediate children with backgrounds or borders. That is useful for general decoration, but cannot identify which subpart is an interactive lens, which is underlying content, or which belongs to a shared glass plane.

Base controls already have useful motion: buttons shrink 3% on press; toggles spring between positions; sliders directly follow dragging and animate programmatic changes; tabs spring an underline. Menus animate shadow strength, while dialogs slightly scale their background on entry. None coordinates glass geometry, optics, and foreground state. Closing menus/dialogs become undisplayed without a corresponding material exit. No dedicated sheet implementation appears in the reviewed base package.

Reduce Transparency and Reduce Motion have real support. Styled backdrop effects can be removed and backgrounds made solid; ordinary animations can jump to targets. However, the standalone glass component paints the theme’s `surface` without forcing its alpha opaque, so translucent themes need additional coverage. The reviewed material path has no Increase Contrast policy.

Existing tests establish static optical output, bridges, semantic stability, and CPU/GPU comparisons. They do not establish the complete experience. In particular, the current “bevel bends” assertion changes tint, rim, and adaptation alongside refraction, so pixel differences do not isolate lensing.

**3. A Liquid Glass design for openmui**

The following values are **proposed openmui tuning**, not claimed Apple constants.

Retain `glass` as a frosted-content style. Develop the Liquid Glass experience around functional surfaces: floating toolbars, navigation, media controls, contextual menus, and temporary interactive lenses. Settings cards and charts remain ordinary content.

Use regular glass by default. Clear glass requires an explicit media-oriented context and a legibility treatment. A material family owns geometry, optics, foreground colors, shadow, motion, and accessibility policy together.

Component behavior should be:

| Component | Appearance and response |
|---|---|
| **Button** | Capsule for spacious controls; rounded rectangle for dense desktop controls. A primary action receives translucent accent tint. Press compresses the silhouette by roughly 2%, slightly flattens its height profile, and creates illumination at contact. Release restores curvature and highlight with a short spring. Labels stay sharp. |
| **Toggle** | Quiet capsule track and readable thumb at rest. During interaction, the thumb becomes a translucent lens, stretching modestly toward travel before settling. The checked position remains explicit; track color supplements position. |
| **Slider** | Ordinary track with visible fill and ticks. The thumb lifts into glass while pressed, widening slightly along movement. Position follows input exactly; only deformation trails. The material reveals the track beneath it. Release removes deformation without moving the selected value. |
| **Tabs** | One shared navigation surface. Selection moves a thin filled capsule within it, without adding another refracting pane. A navigation-bar variant can minimize on scroll; ordinary content tabs retain their compact form. |
| **Menu** | The trigger silhouette expands into a rounded menu, increasing scattering, effective thickness, and shadow. Labels appear as space becomes available. Rows use lightweight fills. Dismissal reverses toward the trigger. |
| **Sheet/dialog** | A compact contextual presentation grows from its source. Partial-height sheets retain inset glass edges; expansion increases opacity and eventually produces an anchored, opaque content surface. Embedded controls share its material context. |
| **Toolbar groups** | Related actions share one surface. Unrelated actions retain deliberate separation. Pointer feedback remains quieter than touch feedback. |

Use a small motion vocabulary, all interruptible:

| Motion | Initial tuning |
|---|---|
| Contact illumination | 60–80 ms rise; 120 ms decay |
| Press deformation/release | Spring: mass 1, stiffness 500, damping 36 |
| Toggle/selection movement | Spring: mass 1, stiffness 400, damping 32 |
| Menu and merge/split transition | Spring: mass 1, stiffness 320, damping 30 |
| Sheet presentation | Spring: mass 1, stiffness 260, damping 30 |
| Foreground reveal | 100–140 ms |
| Background adaptation | 120–180 ms |

These springs should produce a short response with little overshoot. Validate characteristic completion around 200–350 ms for controls and 350–500 ms for larger presentations. A spring’s actual resting time depends on displacement and velocity; do not truncate every spring at a nominal duration.

Overlap follows explicit rules. Compatible shapes in one container share one backdrop, one union silhouette, one optical boundary, and one shadow. Their overlapping interiors do not double their tint or blur. Proximity merging is opt-in by group; unrelated controls do not fuse merely because their screen bounds touch.

Controls inside a glass surface use fills and highlights from that surface. A selected tab, pressed menu row, and toolbar button therefore remain distinct without additional refraction.

A menu promoted into the overlay system retains its source identity. During expansion, source and destination contribute to one transitional material. Afterward, the source stops drawing a duplicate surface under the menu. Separate modal planes occlude or flatten lower material within the overlapping footprint rather than producing accidental glass-on-glass stacks.

State treatment must be consistent:

- **Resting:** quiet highlights, no ambient wobble.
- **Hovered:** modest illumination and shadow change.
- **Pressed/dragging:** contact glow, controlled deformation, responsive optical change.
- **Focused:** stable contrasting focus indicator, independent of shimmer.
- **Selected/checked:** persistent position, marker, or fill plus semantics.
- **Disabled:** subdued foreground and no contact response, while preserving surface structure.
- **Inactive window:** reduced prominence without destroying readability.

Reduce Transparency replaces optical transmission with a truly opaque surface. Increase Contrast strengthens foreground, boundary, and focus separation. Reduce Motion removes elastic deformation, traveling highlights, and animated lens changes; essential geometry changes resolve directly.

**4. Implementation plan, ordered by impact**

**First: correct usage and make the workshop a behavioral acceptance tool.** Remove blanket region coating from the future Liquid Glass pack. Add a real floating toolbar, navigation bar, menu, and contextual sheet over scrollable content. Keep settings and dashboard functionality.

Provide selectable backgrounds: fine text, a checkerboard, high-contrast lines, photography, and light/dark regions. Add timeline scrubbing and deterministic interaction playback. The signature sequence should press a control, expand its menu, merge nearby participants, split them again, and scroll content beneath navigation. A static screenshot cannot prove that sequence.

Define contract-first material participation and transitions before implementation. Extend the existing `openmui.ui.liquid_glass` package rather than introducing a competing container.

**Second: make group geometry and composition correct.** Replace heuristic direct-child extraction with explicit contributions containing stable ID, container ID, material family, current geometry, transform, clip, union membership, and transition relationship.

Collect resolved geometry before painting. Shapes must match the visual positions used for that frame, including translation, scale, scroll, visibility, and clipping. Components publish their surfaces through public runtime APIs; the renderer receives geometry without knowing about buttons or tabs.

Maintain transition records keyed by explicit IDs. Preserve source/destination geometry and animation history across frames. Retain exiting shapes for rendering without retaining actionable semantic ghosts. Interrupted transitions continue from current position and velocity.

Reuse the existing polynomial smooth minimum:

`smin(a,b,k) = min(a,b) − max(k − |a−b|,0)² / (4k)`

Its `k` is a smoothing width, not automatically Apple-style container spacing. Calibrate the relationship against known shape gaps. At zero smoothing, overlapping shapes still form a hard union. Since repeated smooth minimum is order-dependent, use stable contribution ordering and test it.

Replace silent eight-shape truncation with bounded, observable batching or a shape buffer. Pool storage and resolve each group once per frame.

**Third: establish shared backdrop scope in the effect/layer model.** Preserve generic effects as operations over a node’s subtree. Add explicit material participation within that scope.

The renderer should capture the content plane before participating glass, shade the group once, then composite its foreground. This avoids sampling an earlier sibling’s glass. Separate groups can share a capture when their content plane and sampling requirements match.

Interactive lenses also require an underlay/foreground distinction. A slider’s track must render below its glass thumb; labels and focus indicators render above it. Treating the whole slider as one opaque source painted over a glass backdrop cannot reveal its own track through the thumb.

Declare capture, blur, material, and composition dependencies in the existing render graph. Expand sampling bounds by maximum displacement, blur support, union growth, and shadow reach. Damage propagation must include this input footprint as well as previous and current output bounds.

Any necessary portable display-contract additions belong in the standard submodule, following the repository’s contract and layering rules.

**Fourth: improve optics with independently controlled geometry.** Separate bevel width, surface height, refractive strength, and roughness. Derive a height field `h(d)` from signed distance, using a smooth rounded or superelliptic edge profile. Compute a 3D normal:

`N = normalize(-∂h/∂x, -∂h/∂y, 1)`

Use that normal to derive a bounded refractive sample displacement. A physically inspired approximation is sufficient if its direction, magnification, continuity, and cost are controlled. Validate displacement against a grid instead of relying on a “glass-looking” wallpaper.

Add a restrained specular lobe and Fresnel-weighted edge response. Drive lighting from explicit interaction and optional recorded motion inputs. Add inner-edge shading so depth survives on bright backgrounds. Keep dispersion small, edge-localized, and independently switchable.

Separate sharp refractive sampling from low-frequency scattering. A shared blur pyramid can supply regular-material softness without erasing every optical edge.

**Fifth: implement adaptation and states together.** Sample low-resolution backdrop luminance, color, and local contrast. Use those statistics to select material tint, scattering, foreground polarity, and shadow strength. Larger surfaces should adapt conservatively to avoid distracting whole-panel flips.

Specify color space and alpha handling. Contrast decisions must consider the final foreground/background combination, including tint and overlays. The current post-tint brightness adjustment cannot guarantee this.

Temporal adaptation must remain seekable. Use explicit animation segments driven by timestamped backdrop statistics or recorded input, rather than an accumulator dependent on how many frames happened to render.

Implement the component behaviors and accessibility substitutions through shared tokens and material state, including a genuine sheet contract and reversible menu exits.

**Sixth: optimize and prove the result.** GPU work should use the existing RHI and render graph, pooled targets, bounded captures, shared blur levels, and dirty-region caching. Precompute distance/normal fields when repeated shape evaluation becomes expensive.

Current glass evaluates the shape field for coverage and four neighboring gradient samples, plus three bilinear backdrop samples per pixel. Larger groups multiply that cost. Four full-screen RGBA8 surfaces at 1080p already occupy about **32 MiB**, before additional caches.

The existing [budgets](/home/speak/workspace/mui/tests/bench/budgets.bench.omui.json:35) allow **8.33 ms GPU p95** for the ten-panel scene and **250 ms CPU p95**. The CPU budget is a preview/reference target. New measurements must include integrated GPUs, moving backdrops, menu expansion, and merging groups.

Keep a complete deterministic CPU reference. Optimize through pooled scratch storage, cached fields, bounded regions, and parallel rows. Offer an explicit simplified interactive fallback while retaining full reference rendering for headless acceptance. Report skipped capabilities and quality selection.

Use `hanim` analytic motions and `openmui.math`; disable fast-math and contraction. Pin viewport, scale, fonts, assets, material version, event timestamps, lighting inputs, and quality. Compare CPU pixels exactly; use documented GPU tolerances rather than assuming all hardware produces identical floating-point results.

Acceptance tests should prove individual behaviors:

- **Optics:** isolate refraction, dispersion, blur, and lighting; measure line displacement and edge continuity.
- **Adaptation:** move text and bright/dark imagery underneath; verify readable foregrounds, stable polarity, and shadow response.
- **Union:** verify one material in overlaps, no duplicate seam, continuous bridges, and correct splitting.
- **Motion:** sample press, release, interruption, reversal, menu expansion, and exits at fixed timestamps.
- **Layering:** exercise sibling groups, nested effects, overlays, clips, transforms, and opacity.
- **Accessibility:** test all three preferences independently and together, including translucent themes.
- **Determinism:** compare sequential playback, direct seeking, reordered frame rendering, and serial/parallel CPU output.
- **Semantics:** preserve IDs, actions, selection, focus, and hit targets; decorative bridges never become controls.
- **Performance:** record allocation counts, capture area, passes, texture memory, and frame percentiles in CI.

The acceptance milestone should be a working interaction sequence with matching semantic, visual, and timing evidence. That would demonstrate the missing material system far more convincingly than another pinned image of frosted cards.