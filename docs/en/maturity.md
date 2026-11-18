# Maturity

Every package of omui 0.1 carries one of three labels in its manifest (`[package.metadata.openmui] maturity`; release plan §2.1, D-189). The label is a promise about the package's default public surface; quality and the stability of its interface are two separate things it speaks for.

| Label | Quality | Stability | Public claims | May be depended on by |
|---|---|---|---|---|
| **Stable** | every quality gate of the release (plan §8) | source-compatible within 0.1.x; specification-level names follow the specification's version | in the claims ledger | everything |
| **Preview** | every quality gate, within its stated scope — narrow and complete | the interface will change; each change is listed in the release notes | in the claims ledger, with its scope | preview, experimental packages and works |
| **Experimental** | the basic gate: it builds on every tier-1 platform, one headless check shows it holds, its README says its target and gap | none | none; it is on the [Experimental](experimental.md) page with its target and gap | experimental packages and works only |

A capability below its package's label is marked in its project's feature table and kept behind a feature. The lint (`maturity`) holds every library to depend by default only on packages at its label or above.

## By project

| Project | stable | preview | experimental |
|---|---|---|---|
| omui | 28 | 2 | 1 |
| hanim | 5 | 4 | 0 |
| xrender | 10 | 8 | 0 |
| xengine | 0 | 21 | 2 |
| gongfu | 0 | 0 | 5 |
| xtools | 1 | 0 | 2 |
| mlibs | 26 | 4 | 0 |
| examples | 12 | 0 | 0 |
| build plugins (mcpp) | 1 | 0 | 0 |
| works (showcase) | 0 | 10 | 5 |
| site | 0 | 1 | 0 |
| repository tests | 5 | 0 | 0 |
| repository tools | 1 | 0 | 0 |

### omui

| Package | Directory | Label |
|---|---|---|
| `omui.abi` | [modules/projects/omui/abi](../../modules/projects/omui/abi) | stable |
| `omui.accessibility` | [modules/projects/omui/accessibility](../../modules/projects/omui/accessibility) | stable |
| `omui.agent` | [modules/projects/omui/agent](../../modules/projects/omui/agent) | stable |
| `omui.core` | [modules/projects/omui/core](../../modules/projects/omui/core) | stable |
| `omui.sdk` | [modules/projects/omui/sdk](../../modules/projects/omui/sdk) | stable |
| `omui.expr` | [modules/projects/omui/expr](../../modules/projects/omui/expr) | stable |
| `omui.host.headless` | [modules/projects/omui/host/headless](../../modules/projects/omui/host/headless) | stable |
| `omui.host.android` | [modules/projects/omui/host/android](../../modules/projects/omui/host/android) | preview |
| `omui.host.sdl3` | [modules/projects/omui/host/sdl3](../../modules/projects/omui/host/sdl3) | stable |
| `omui.host.web` | [modules/projects/omui/host/web](../../modules/projects/omui/host/web) | stable |
| `omui.native` | [modules/projects/omui/native](../../modules/projects/omui/native) | stable |
| `omui.player` | [modules/projects/omui/player](../../modules/projects/omui/player) | stable |
| `omui.designer` | [modules/projects/omui/designer](../../modules/projects/omui/designer) | preview |
| `omui.reflect` | [modules/projects/omui/reflect](../../modules/projects/omui/reflect) | experimental |
| `omui.semantic` | [modules/projects/omui/semantic](../../modules/projects/omui/semantic) | stable |
| `omui.theme` | [modules/projects/omui/theme](../../modules/projects/omui/theme) | stable |
| `omui.view` | [modules/projects/omui/view](../../modules/projects/omui/view) | stable |
| `omui.viewdoc` | [modules/projects/omui/viewdoc](../../modules/projects/omui/viewdoc) | stable |
| `omui.ui.base` | [modules/projects/omui/components/base](../../modules/projects/omui/components/base) | stable |
| `omui.ui.canvas` | [modules/projects/omui/components/canvas](../../modules/projects/omui/components/canvas) | stable |
| `omui.ui.markdown` | [modules/projects/omui/components/markdown](../../modules/projects/omui/components/markdown) | stable |
| `omui.ui.timeline` | [modules/projects/omui/components/timeline](../../modules/projects/omui/components/timeline) | stable |
| `omui.lang` | [modules/projects/omui/lang](../../modules/projects/omui/lang) | stable |
| `omui.lang.js` | [modules/projects/omui/lang/js](../../modules/projects/omui/lang/js) | stable |
| `omui.lang.ts` | [modules/projects/omui/lang/ts](../../modules/projects/omui/lang/ts) | stable |
| `omui.cli` | [modules/projects/omui/tools/cli](../../modules/projects/omui/tools/cli) | stable |
| `omui.gen` | [modules/projects/omui/tools/gen](../../modules/projects/omui/tools/gen) | stable |
| `omui.mcp` | [modules/projects/omui/tools/mcp](../../modules/projects/omui/tools/mcp) | stable |
| `omui.entry.player` | [modules/projects/omui/entry/player](../../modules/projects/omui/entry/player) | stable |
| `omui.entry.devhost` | [modules/projects/omui/entry/devhost](../../modules/projects/omui/entry/devhost) | stable |
| `omui.cts` | [modules/projects/omui/cts](../../modules/projects/omui/cts) | stable |

### hanim

| Package | Directory | Label |
|---|---|---|
| `hanim.core` | [modules/projects/hanim/core](../../modules/projects/hanim/core) | stable |
| `hanim` | [modules/projects/hanim](../../modules/projects/hanim) | stable |
| `hanim.timeline` | [modules/projects/hanim/timeline](../../modules/projects/hanim/timeline) | stable |
| `hanim.timeline.tool` | [modules/projects/hanim/timeline/tool](../../modules/projects/hanim/timeline/tool) | stable |
| `hanim.visual` | [modules/projects/hanim/visual](../../modules/projects/hanim/visual) | stable |
| `hanim.rhi` | [modules/projects/hanim/rhi](../../modules/projects/hanim/rhi) | preview |
| `hanim.studio` | [modules/projects/hanim/studio](../../modules/projects/hanim/studio) | preview |
| `hanim.skeleton` | [modules/projects/hanim/skeleton](../../modules/projects/hanim/skeleton) | preview |
| `hanim.skeleton.cts` | [modules/projects/hanim/skeleton/cts](../../modules/projects/hanim/skeleton/cts) | preview |

### xrender

| Package | Directory | Label |
|---|---|---|
| `xrender.raster` | [modules/projects/xrender/raster](../../modules/projects/xrender/raster) | stable |
| `xrender.text` | [modules/projects/xrender/text](../../modules/projects/xrender/text) | stable |
| `xrender.graph` | [modules/projects/xrender/graph](../../modules/projects/xrender/graph) | stable |
| `xrender.cts` | [modules/projects/xrender/cts](../../modules/projects/xrender/cts) | stable |
| `xrender.vector` | [modules/projects/xrender/vector](../../modules/projects/xrender/vector) | stable |
| `xrender.frame` | [modules/projects/xrender/frame](../../modules/projects/xrender/frame) | stable |
| `xrender.scene` | [modules/projects/xrender/scene](../../modules/projects/xrender/scene) | preview |
| `xrender.mesh` | [modules/projects/xrender/mesh](../../modules/projects/xrender/mesh) | preview |
| `xrender.particle` | [modules/projects/xrender/particle](../../modules/projects/xrender/particle) | preview |
| `xrender.particle.gpu` | [modules/projects/xrender/particle/gpu](../../modules/projects/xrender/particle/gpu) | preview |
| `xrender.mesh.gpu` | [modules/projects/xrender/mesh/gpu](../../modules/projects/xrender/mesh/gpu) | preview |
| `xrender.mesh.gpu.cts` | [modules/projects/xrender/mesh/gpu/cts](../../modules/projects/xrender/mesh/gpu/cts) | preview |
| `xrender.asset` | [modules/projects/xrender/asset](../../modules/projects/xrender/asset) | preview |
| `xrender.effect` | [modules/projects/xrender/effect](../../modules/projects/xrender/effect) | stable |
| `xrender.rhi.sdl` | [modules/projects/xrender/rhi/sdl](../../modules/projects/xrender/rhi/sdl) | stable |
| `xrender.tool` | [modules/projects/xrender/tool](../../modules/projects/xrender/tool) | preview |
| `xrender.rhi.webgpu` | [modules/projects/xrender/rhi/webgpu](../../modules/projects/xrender/rhi/webgpu) | stable |
| `xrender.rhi.webgl` | [modules/projects/xrender/rhi/webgl](../../modules/projects/xrender/rhi/webgl) | stable |

### xengine

| Package | Directory | Label |
|---|---|---|
| `xengine.dynamics` | [modules/projects/xengine/kernel/dynamics](../../modules/projects/xengine/kernel/dynamics) | preview |
| `xengine.cellular` | [modules/projects/xengine/kernel/cellular](../../modules/projects/xengine/kernel/cellular) | preview |
| `xengine.matter` | [modules/projects/xengine/kernel/matter](../../modules/projects/xengine/kernel/matter) | preview |
| `xengine.particles` | [modules/projects/xengine/kernel/particles](../../modules/projects/xengine/kernel/particles) | experimental |
| `xengine.field` | [modules/projects/xengine/kernel/field](../../modules/projects/xengine/kernel/field) | experimental |
| `xengine.matter.cts` | [modules/projects/xengine/kernel/matter/cts](../../modules/projects/xengine/kernel/matter/cts) | preview |
| `xengine.matter.lab` | [modules/projects/xengine/kernel/matter/lab](../../modules/projects/xengine/kernel/matter/lab) | preview |
| `xengine.cellular.present` | [modules/projects/xengine/kernel/cellular/present](../../modules/projects/xengine/kernel/cellular/present) | preview |
| `xengine.cellular.rhi` | [modules/projects/xengine/kernel/cellular/rhi](../../modules/projects/xengine/kernel/cellular/rhi) | preview |
| `xengine.cellular.rhi.cts` | [modules/projects/xengine/kernel/cellular/rhi/cts](../../modules/projects/xengine/kernel/cellular/rhi/cts) | preview |
| `xengine.audio` | [modules/projects/xengine/kernel/audio](../../modules/projects/xengine/kernel/audio) | preview |
| `xengine.audio.device` | [modules/projects/xengine/kernel/audio/device](../../modules/projects/xengine/kernel/audio/device) | preview |
| `xengine.video` | [modules/projects/xengine/kernel/video](../../modules/projects/xengine/kernel/video) | preview |
| `xengine.video.webm` | [modules/projects/xengine/kernel/video/webm](../../modules/projects/xengine/kernel/video/webm) | preview |
| `xengine.video.tool` | [modules/projects/xengine/kernel/video/tool](../../modules/projects/xengine/kernel/video/tool) | preview |
| `xengine.audio.timeline` | [modules/projects/xengine/kernel/audio/timeline](../../modules/projects/xengine/kernel/audio/timeline) | preview |
| `xengine.world` | [modules/projects/xengine/game/world](../../modules/projects/xengine/game/world) | preview |
| `xengine.runtime` | [modules/projects/xengine/game/runtime](../../modules/projects/xengine/game/runtime) | preview |
| `xengine.playtest` | [modules/projects/xengine/game/playtest](../../modules/projects/xengine/game/playtest) | preview |
| `xengine.input` | [modules/projects/xengine/game/input](../../modules/projects/xengine/game/input) | preview |
| `xengine.ui` | [modules/projects/xengine/game/ui](../../modules/projects/xengine/game/ui) | preview |
| `xengine.nav` | [modules/projects/xengine/game/nav](../../modules/projects/xengine/game/nav) | preview |
| `xengine.nav.cts` | [modules/projects/xengine/game/nav/cts](../../modules/projects/xengine/game/nav/cts) | preview |

### gongfu

| Package | Directory | Label |
|---|---|---|
| `gongfu` | [modules/projects/gongfu](../../modules/projects/gongfu) | experimental |
| `gongfu.data` | [modules/projects/gongfu/data](../../modules/projects/gongfu/data) | experimental |
| `gongfu.present2d` | [modules/projects/gongfu/present2d](../../modules/projects/gongfu/present2d) | experimental |
| `gongfu.rules` | [modules/projects/gongfu/rules](../../modules/projects/gongfu/rules) | experimental |
| `gongfu.studio` | [modules/projects/gongfu/studio](../../modules/projects/gongfu/studio) | experimental |

### xtools

| Package | Directory | Label |
|---|---|---|
| `xtools` | [modules/projects/xtools](../../modules/projects/xtools) | experimental |
| `xtools.inspector` | [modules/projects/xtools/inspector](../../modules/projects/xtools/inspector) | stable |
| `xtools.audition` | [modules/projects/xtools/audition](../../modules/projects/xtools/audition) | experimental |

### mlibs

| Package | Directory | Label |
|---|---|---|
| `mlibs.control` | [modules/base/control](../../modules/base/control) | stable |
| `mlibs.control.gen` | [modules/base/control/gen](../../modules/base/control/gen) | stable |
| `mlibs.agent` | [modules/base/agent](../../modules/base/agent) | stable |
| `mlibs.agent.cts` | [modules/base/agent/cts](../../modules/base/agent/cts) | stable |
| `mlibs.build` | [modules/base/build](../../modules/base/build) | stable |
| `mlibs.cts` | [modules/base/cts](../../modules/base/cts) | stable |
| `mlibs.dynamics` | [modules/base/dynamics](../../modules/base/dynamics) | preview |
| `mlibs.geom` | [modules/base/geom](../../modules/base/geom) | stable |
| `mlibs.image` | [modules/base/image](../../modules/base/image) | stable |
| `mlibs.json` | [modules/base/json](../../modules/base/json) | stable |
| `mlibs.project` | [modules/base/project](../../modules/base/project) | preview |
| `mlibs.ts` | [modules/base/ts](../../modules/base/ts) | stable |
| `mlibs.xdef` | [modules/base/xdef](../../modules/base/xdef) | stable |
| `mlibs.xdef.tool` | [modules/base/xdef/tool](../../modules/base/xdef/tool) | preview |
| `mlibs.math` | [modules/base/math](../../modules/base/math) | stable |
| `mlibs.memory` | [modules/base/memory](../../modules/base/memory) | stable |
| `mlibs.pipeline` | [modules/base/pipeline](../../modules/base/pipeline) | stable |
| `mpal` | [modules/base/mpal](../../modules/base/mpal) | preview |
| `mlibs.simulation` | [modules/base/simulation](../../modules/base/simulation) | stable |
| `mlibs.unicode` | [modules/base/unicode](../../modules/base/unicode) | stable |
| `mlibs.utils` | [modules/base/utils](../../modules/base/utils) | stable |
| `mlibs.rhi` | [modules/gfx/rhi](../../modules/gfx/rhi) | stable |
| `mlibs.rhi.cts` | [modules/gfx/rhi/cts](../../modules/gfx/rhi/cts) | stable |
| `mlibs.shader` | [modules/gfx/shader](../../modules/gfx/shader) | stable |
| `mlibs.shader.rules` | [modules/gfx/shader/rules](../../modules/gfx/shader/rules) | stable |
| `mlibs.i18n` | [modules/content/i18n](../../modules/content/i18n) | stable |
| `mlibs.i18n.cldr` | [modules/content/i18n/cldr](../../modules/content/i18n/cldr) | stable |
| `mlibs.layout` | [modules/content/layout](../../modules/content/layout) | stable |
| `mlibs.markdown` | [modules/content/markdown](../../modules/content/markdown) | stable |
| `mlibs.text` | [modules/content/text](../../modules/content/text) | stable |

### examples

| Package | Directory | Label |
|---|---|---|
| `omui.example.hello-im` | [examples/01-hello-im](../../examples/01-hello-im) | stable |
| `omui.example.hello-ts` | [examples/02-hello-ts](../../examples/02-hello-ts) | stable |
| `omui.example.counter` | [examples/03-counter](../../examples/03-counter) | stable |
| `omui.example.rating` | [examples/04-rating](../../examples/04-rating) | stable |
| `omui.example.contrast` | [examples/05-contrast](../../examples/05-contrast) | stable |
| `omui.example.theming` | [examples/06-theming](../../examples/06-theming) | stable |
| `omui.example.headless-and-agent` | [examples/07-headless-and-agent](../../examples/07-headless-and-agent) | stable |
| `omui.example.markdown-viewer` | [examples/08-markdown-viewer](../../examples/08-markdown-viewer) | stable |
| `omui.example.markdown-stream` | [examples/09-markdown-stream](../../examples/09-markdown-stream) | stable |
| `omui.example.canvas-basics` | [examples/10-canvas-basics](../../examples/10-canvas-basics) | stable |
| `omui.example.notes` | [examples/12-notes](../../examples/12-notes) | stable |
| `omui.example.live-design` | [examples/13-live-design](../../examples/13-live-design) | stable |

### build plugins (mcpp)

| Package | Directory | Label |
|---|---|---|
| `omui.build` | [mcpp/build](../../mcpp/build) | stable |

### works (showcase)

| Package | Directory | Label |
|---|---|---|
| `omui.showcase.fighting` | [showcase/fighting](../../showcase/fighting) | experimental |
| `omui.showcase.fighting.headless` | [showcase/fighting/headless](../../showcase/fighting/headless) | experimental |
| `omui.showcase.markdown_wall` | [showcase/markdown-wall](../../showcase/markdown-wall) | preview |
| `omui.showcase.promo_video` | [showcase/promo-video](../../showcase/promo-video) | preview |
| `omui.showcase.style_gallery` | [showcase/style-gallery](../../showcase/style-gallery) | preview |
| `omui.showcase.widget_breakout` | [showcase/widget-breakout](../../showcase/widget-breakout) | preview |
| `omui.showcase.widget_breakout.headless` | [showcase/widget-breakout/headless](../../showcase/widget-breakout/headless) | preview |
| `omui.showcase.kiln` | [showcase/kiln](../../showcase/kiln) | preview |
| `omui.showcase.kiln.window` | [showcase/kiln/window](../../showcase/kiln/window) | preview |
| `omui.showcase.quay` | [showcase/quay](../../showcase/quay) | preview |
| `omui.showcase.quay.play` | [showcase/quay/play](../../showcase/quay/play) | preview |
| `omui.showcase.quay.window` | [showcase/quay/window](../../showcase/quay/window) | preview |
| `omui.showcase.glade.headless` | [showcase/glade/headless](../../showcase/glade/headless) | experimental |
| `omui.showcase.swarm.headless` | [showcase/swarm/headless](../../showcase/swarm/headless) | experimental |
| `omui.showcase.swarm.window` | [showcase/swarm/window](../../showcase/swarm/window) | experimental |

### site

| Package | Directory | Label |
|---|---|---|
| `omui.site` | [site](../../site) | preview |

### repository tests

| Package | Directory | Label |
|---|---|---|
| `omui.bench` | [tests/bench](../../tests/bench) | stable |
| `omui.bench.competitors` | [tests/bench/competitors](../../tests/bench/competitors) | stable |
| `omui.size.yard` | [tests/size/yard](../../tests/size/yard) | stable |
| `omui.features` | [tests/features](../../tests/features) | stable |
| `acme.plugins` | [tests/plugins](../../tests/plugins) | stable |

### repository tools

| Package | Directory | Label |
|---|---|---|
| `omui.devtools` | [tools/devtools](../../tools/devtools) | stable |

Generated by `omui-devtools docs` from every package's manifest; do not edit.
