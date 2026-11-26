# 成熟度

omui 0.1 的每个包都在 manifest 里带三种标注之一（`[package.metadata.openmui] maturity`；发布方案 §2.1，D-189）。标注是对包默认公开面的承诺；质量与接口稳定是它分别说明的两件事。

| 标注 | 质量 | 稳定性 | 对外声明 | 可以被谁依赖 |
|---|---|---|---|---|
| **稳定** | 发布的全部质量门（方案 §8） | 0.1.x 内源码兼容；规范层面的名字按规范版本 | 进声明台账 | 所有 |
| **预览** | 在声明的范围内，全部质量门——窄而完整 | 接口会变，变更逐条写进发布说明 | 进声明台账，范围写明 | 预览、实验的包与作品 |
| **实验** | 基本门：一级平台能构建，一条无头验证说明它成立，README 写明目标与差距 | 无 | 不进台账；列在[实验](experimental.md)页，写目标与差距 | 只有实验的包与作品 |

包内个别能力低于包的标注的，在项目的特性表里单独标出，并由 feature 隔开。lint `maturity` 检查每个库默认只依赖标注不低于自己的包。

## 按项目

| 项目 | 稳定 | 预览 | 实验 |
|---|---|---|---|
| omui | 30 | 4 | 1 |
| hanim | 5 | 4 | 0 |
| mrender | 11 | 9 | 0 |
| meng | 0 | 21 | 2 |
| gongfu | 0 | 0 | 5 |
| xtools | 1 | 0 | 2 |
| mlibs | 32 | 5 | 0 |
| 例子 | 12 | 0 | 0 |
| 构建插件（mcpp） | 1 | 0 | 0 |
| 作品（showcase） | 0 | 10 | 5 |
| 官网 | 0 | 1 | 0 |
| 仓库测试 | 6 | 2 | 0 |
| 仓库工具 | 1 | 0 | 0 |

### omui

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.abi` | [modules/projects/omui/abi](../../modules/projects/omui/abi) | 稳定 |
| `omui.accessibility` | [modules/projects/omui/accessibility](../../modules/projects/omui/accessibility) | 稳定 |
| `omui.accessibility.bridge` | [modules/projects/omui/accessibility/bridge](../../modules/projects/omui/accessibility/bridge) | 稳定 |
| `omui.agent` | [modules/projects/omui/agent](../../modules/projects/omui/agent) | 稳定 |
| `omui.core` | [modules/projects/omui/core](../../modules/projects/omui/core) | 稳定 |
| `omui.sdk` | [modules/projects/omui/sdk](../../modules/projects/omui/sdk) | 稳定 |
| `omui.expr` | [modules/projects/omui/expr](../../modules/projects/omui/expr) | 稳定 |
| `omui.host.headless` | [modules/projects/omui/host/headless](../../modules/projects/omui/host/headless) | 稳定 |
| `omui.host.terminal` | [modules/projects/omui/host/terminal](../../modules/projects/omui/host/terminal) | 预览 |
| `omui.host.android` | [modules/projects/omui/host/android](../../modules/projects/omui/host/android) | 预览 |
| `omui.host.sdl3` | [modules/projects/omui/host/sdl3](../../modules/projects/omui/host/sdl3) | 稳定 |
| `omui.host.sdl3.gpu` | [modules/projects/omui/host/sdl3/gpu](../../modules/projects/omui/host/sdl3/gpu) | 稳定 |
| `omui.host.web` | [modules/projects/omui/host/web](../../modules/projects/omui/host/web) | 稳定 |
| `omui.native` | [modules/projects/omui/native](../../modules/projects/omui/native) | 稳定 |
| `omui.player` | [modules/projects/omui/player](../../modules/projects/omui/player) | 稳定 |
| `omui.designer` | [modules/projects/omui/designer](../../modules/projects/omui/designer) | 预览 |
| `omui.reflect` | [modules/projects/omui/reflect](../../modules/projects/omui/reflect) | 实验 |
| `omui.semantic` | [modules/projects/omui/semantic](../../modules/projects/omui/semantic) | 稳定 |
| `omui.theme` | [modules/projects/omui/theme](../../modules/projects/omui/theme) | 稳定 |
| `omui.view` | [modules/projects/omui/view](../../modules/projects/omui/view) | 稳定 |
| `omui.viewdoc` | [modules/projects/omui/viewdoc](../../modules/projects/omui/viewdoc) | 稳定 |
| `omui.ui.base` | [modules/projects/omui/components/base](../../modules/projects/omui/components/base) | 稳定 |
| `omui.ui.canvas` | [modules/projects/omui/components/canvas](../../modules/projects/omui/components/canvas) | 稳定 |
| `omui.ui.markdown` | [modules/projects/omui/components/markdown](../../modules/projects/omui/components/markdown) | 稳定 |
| `omui.ui.timeline` | [modules/projects/omui/components/timeline](../../modules/projects/omui/components/timeline) | 稳定 |
| `omui.lang` | [modules/projects/omui/lang](../../modules/projects/omui/lang) | 稳定 |
| `omui.lang.js` | [modules/projects/omui/lang/js](../../modules/projects/omui/lang/js) | 稳定 |
| `omui.lang.ts` | [modules/projects/omui/lang/ts](../../modules/projects/omui/lang/ts) | 稳定 |
| `omui.cli` | [modules/projects/omui/tools/cli](../../modules/projects/omui/tools/cli) | 稳定 |
| `omui.gen` | [modules/projects/omui/tools/gen](../../modules/projects/omui/tools/gen) | 稳定 |
| `omui.mcp` | [modules/projects/omui/tools/mcp](../../modules/projects/omui/tools/mcp) | 稳定 |
| `omui.entry.player` | [modules/projects/omui/entry/player](../../modules/projects/omui/entry/player) | 稳定 |
| `omui.entry.devhost` | [modules/projects/omui/entry/devhost](../../modules/projects/omui/entry/devhost) | 稳定 |
| `omui.entry.launcher` | [modules/projects/omui/entry/launcher](../../modules/projects/omui/entry/launcher) | 预览 |
| `omui.cts` | [modules/projects/omui/cts](../../modules/projects/omui/cts) | 稳定 |

### hanim

| 包 | 目录 | 标注 |
|---|---|---|
| `hanim.core` | [modules/projects/hanim/core](../../modules/projects/hanim/core) | 稳定 |
| `sunrisepeak.hanim` | [modules/projects/hanim](../../modules/projects/hanim) | 稳定 |
| `hanim.timeline` | [modules/projects/hanim/timeline](../../modules/projects/hanim/timeline) | 稳定 |
| `hanim.timeline.tool` | [modules/projects/hanim/timeline/tool](../../modules/projects/hanim/timeline/tool) | 稳定 |
| `hanim.visual` | [modules/projects/hanim/visual](../../modules/projects/hanim/visual) | 稳定 |
| `hanim.rhi` | [modules/projects/hanim/rhi](../../modules/projects/hanim/rhi) | 预览 |
| `hanim.studio` | [modules/projects/hanim/studio](../../modules/projects/hanim/studio) | 预览 |
| `hanim.skeleton` | [modules/projects/hanim/skeleton](../../modules/projects/hanim/skeleton) | 预览 |
| `hanim.skeleton.cts` | [modules/projects/hanim/skeleton/cts](../../modules/projects/hanim/skeleton/cts) | 预览 |

### mrender

| 包 | 目录 | 标注 |
|---|---|---|
| `mrender.raster` | [modules/projects/mrender/raster](../../modules/projects/mrender/raster) | 稳定 |
| `mrender.text` | [modules/projects/mrender/text](../../modules/projects/mrender/text) | 稳定 |
| `mrender.graph` | [modules/projects/mrender/graph](../../modules/projects/mrender/graph) | 稳定 |
| `mrender.cts` | [modules/projects/mrender/cts](../../modules/projects/mrender/cts) | 稳定 |
| `mrender.cells` | [modules/projects/mrender/cells](../../modules/projects/mrender/cells) | 预览 |
| `mrender.display` | [modules/projects/mrender/display](../../modules/projects/mrender/display) | 稳定 |
| `mrender.vector` | [modules/projects/mrender/vector](../../modules/projects/mrender/vector) | 稳定 |
| `mrender.frame` | [modules/projects/mrender/frame](../../modules/projects/mrender/frame) | 稳定 |
| `mrender.scene` | [modules/projects/mrender/scene](../../modules/projects/mrender/scene) | 预览 |
| `mrender.mesh` | [modules/projects/mrender/mesh](../../modules/projects/mrender/mesh) | 预览 |
| `mrender.particle` | [modules/projects/mrender/particle](../../modules/projects/mrender/particle) | 预览 |
| `mrender.particle.gpu` | [modules/projects/mrender/particle/gpu](../../modules/projects/mrender/particle/gpu) | 预览 |
| `mrender.mesh.gpu` | [modules/projects/mrender/mesh/gpu](../../modules/projects/mrender/mesh/gpu) | 预览 |
| `mrender.mesh.gpu.cts` | [modules/projects/mrender/mesh/gpu/cts](../../modules/projects/mrender/mesh/gpu/cts) | 预览 |
| `mrender.asset` | [modules/projects/mrender/asset](../../modules/projects/mrender/asset) | 预览 |
| `mrender.effect` | [modules/projects/mrender/effect](../../modules/projects/mrender/effect) | 稳定 |
| `mrender.rhi.sdl` | [modules/projects/mrender/rhi/sdl](../../modules/projects/mrender/rhi/sdl) | 稳定 |
| `mrender.tool` | [modules/projects/mrender/tool](../../modules/projects/mrender/tool) | 预览 |
| `mrender.rhi.webgpu` | [modules/projects/mrender/rhi/webgpu](../../modules/projects/mrender/rhi/webgpu) | 稳定 |
| `mrender.rhi.webgl` | [modules/projects/mrender/rhi/webgl](../../modules/projects/mrender/rhi/webgl) | 稳定 |

### meng

| 包 | 目录 | 标注 |
|---|---|---|
| `meng.dynamics` | [modules/projects/meng/kernel/dynamics](../../modules/projects/meng/kernel/dynamics) | 预览 |
| `meng.cellular` | [modules/projects/meng/kernel/cellular](../../modules/projects/meng/kernel/cellular) | 预览 |
| `meng.matter` | [modules/projects/meng/kernel/matter](../../modules/projects/meng/kernel/matter) | 预览 |
| `meng.particles` | [modules/projects/meng/kernel/particles](../../modules/projects/meng/kernel/particles) | 实验 |
| `meng.field` | [modules/projects/meng/kernel/field](../../modules/projects/meng/kernel/field) | 实验 |
| `meng.matter.cts` | [modules/projects/meng/kernel/matter/cts](../../modules/projects/meng/kernel/matter/cts) | 预览 |
| `meng.matter.lab` | [modules/projects/meng/kernel/matter/lab](../../modules/projects/meng/kernel/matter/lab) | 预览 |
| `meng.cellular.present` | [modules/projects/meng/kernel/cellular/present](../../modules/projects/meng/kernel/cellular/present) | 预览 |
| `meng.cellular.rhi` | [modules/projects/meng/kernel/cellular/rhi](../../modules/projects/meng/kernel/cellular/rhi) | 预览 |
| `meng.cellular.rhi.cts` | [modules/projects/meng/kernel/cellular/rhi/cts](../../modules/projects/meng/kernel/cellular/rhi/cts) | 预览 |
| `meng.audio` | [modules/projects/meng/kernel/audio](../../modules/projects/meng/kernel/audio) | 预览 |
| `meng.audio.device` | [modules/projects/meng/kernel/audio/device](../../modules/projects/meng/kernel/audio/device) | 预览 |
| `meng.video` | [modules/projects/meng/kernel/video](../../modules/projects/meng/kernel/video) | 预览 |
| `meng.video.webm` | [modules/projects/meng/kernel/video/webm](../../modules/projects/meng/kernel/video/webm) | 预览 |
| `meng.video.tool` | [modules/projects/meng/kernel/video/tool](../../modules/projects/meng/kernel/video/tool) | 预览 |
| `meng.audio.timeline` | [modules/projects/meng/kernel/audio/timeline](../../modules/projects/meng/kernel/audio/timeline) | 预览 |
| `meng.world` | [modules/projects/meng/game/world](../../modules/projects/meng/game/world) | 预览 |
| `meng.runtime` | [modules/projects/meng/game/runtime](../../modules/projects/meng/game/runtime) | 预览 |
| `meng.playtest` | [modules/projects/meng/game/playtest](../../modules/projects/meng/game/playtest) | 预览 |
| `meng.input` | [modules/projects/meng/game/input](../../modules/projects/meng/game/input) | 预览 |
| `meng.ui` | [modules/projects/meng/game/ui](../../modules/projects/meng/game/ui) | 预览 |
| `meng.nav` | [modules/projects/meng/game/nav](../../modules/projects/meng/game/nav) | 预览 |
| `meng.nav.cts` | [modules/projects/meng/game/nav/cts](../../modules/projects/meng/game/nav/cts) | 预览 |

### gongfu

| 包 | 目录 | 标注 |
|---|---|---|
| `gongfu` | [modules/projects/gongfu](../../modules/projects/gongfu) | 实验 |
| `gongfu.data` | [modules/projects/gongfu/data](../../modules/projects/gongfu/data) | 实验 |
| `gongfu.present2d` | [modules/projects/gongfu/present2d](../../modules/projects/gongfu/present2d) | 实验 |
| `gongfu.rules` | [modules/projects/gongfu/rules](../../modules/projects/gongfu/rules) | 实验 |
| `gongfu.studio` | [modules/projects/gongfu/studio](../../modules/projects/gongfu/studio) | 实验 |

### xtools

| 包 | 目录 | 标注 |
|---|---|---|
| `xtools` | [modules/projects/xtools](../../modules/projects/xtools) | 实验 |
| `xtools.inspector` | [modules/projects/xtools/inspector](../../modules/projects/xtools/inspector) | 稳定 |
| `xtools.audition` | [modules/projects/xtools/audition](../../modules/projects/xtools/audition) | 实验 |

### mlibs

| 包 | 目录 | 标注 |
|---|---|---|
| `mlibs.control` | [modules/base/control](../../modules/base/control) | 稳定 |
| `mlibs.control.gen` | [modules/base/control/gen](../../modules/base/control/gen) | 稳定 |
| `mlibs.agent` | [modules/base/agent](../../modules/base/agent) | 稳定 |
| `mlibs.agent.cts` | [modules/base/agent/cts](../../modules/base/agent/cts) | 稳定 |
| `mlibs.build` | [modules/base/build](../../modules/base/build) | 稳定 |
| `mlibs.compose` | [modules/base/compose](../../modules/base/compose) | 预览 |
| `mlibs.cts` | [modules/base/cts](../../modules/base/cts) | 稳定 |
| `mlibs.dynamics` | [modules/base/dynamics](../../modules/base/dynamics) | 预览 |
| `mlibs.geom` | [modules/base/geom](../../modules/base/geom) | 稳定 |
| `mlibs.image` | [modules/base/image](../../modules/base/image) | 稳定 |
| `mlibs.json` | [modules/base/json](../../modules/base/json) | 稳定 |
| `mlibs.project` | [modules/base/project](../../modules/base/project) | 预览 |
| `mlibs.ts` | [modules/base/ts](../../modules/base/ts) | 稳定 |
| `mlibs.xdef` | [modules/base/xdef](../../modules/base/xdef) | 稳定 |
| `mlibs.xdef.tool` | [modules/base/xdef/tool](../../modules/base/xdef/tool) | 预览 |
| `mlibs.math` | [modules/base/math](../../modules/base/math) | 稳定 |
| `mlibs.memory` | [modules/base/memory](../../modules/base/memory) | 稳定 |
| `mlibs.pipeline` | [modules/base/pipeline](../../modules/base/pipeline) | 稳定 |
| `mcpplibs.mpal` | [modules/base/mpal](../../modules/base/mpal) | 预览 |
| `mlibs.simulation` | [modules/base/simulation](../../modules/base/simulation) | 稳定 |
| `mlibs.unicode` | [modules/base/unicode](../../modules/base/unicode) | 稳定 |
| `mlibs.utils` | [modules/base/utils](../../modules/base/utils) | 稳定 |
| `mlibs.rhi` | [modules/gfx/rhi](../../modules/gfx/rhi) | 稳定 |
| `mlibs.rhi.cts` | [modules/gfx/rhi/cts](../../modules/gfx/rhi/cts) | 稳定 |
| `mlibs.shader` | [modules/gfx/shader](../../modules/gfx/shader) | 稳定 |
| `mlibs.shader.rules` | [modules/gfx/shader/rules](../../modules/gfx/shader/rules) | 稳定 |
| `mlibs.i18n` | [modules/content/i18n](../../modules/content/i18n) | 稳定 |
| `mlibs.i18n.cldr` | [modules/content/i18n/cldr](../../modules/content/i18n/cldr) | 稳定 |
| `mlibs.layout` | [modules/content/layout](../../modules/content/layout) | 稳定 |
| `mlibs.markdown` | [modules/content/markdown](../../modules/content/markdown) | 稳定 |
| `mlibs.text` | [modules/content/text](../../modules/content/text) | 稳定 |
| `mlibs.text.harfbuzz` | [modules/content/text/harfbuzz](../../modules/content/text/harfbuzz) | 稳定 |
| `mlibs.text.fonts` | [modules/content/text/fonts](../../modules/content/text/fonts) | 稳定 |
| `mlibs.text.fonts.han` | [modules/content/text/fonts/han](../../modules/content/text/fonts/han) | 稳定 |
| `mlibs.text.fonts.arabic` | [modules/content/text/fonts/arabic](../../modules/content/text/fonts/arabic) | 稳定 |
| `mlibs.text.fonts.emoji` | [modules/content/text/fonts/emoji](../../modules/content/text/fonts/emoji) | 稳定 |
| `mlibs.text.fonts.mono` | [modules/content/text/fonts/mono](../../modules/content/text/fonts/mono) | 稳定 |

### 例子

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.example.hello-im` | [examples/01-hello-im](../../examples/01-hello-im) | 稳定 |
| `omui.example.hello-ts` | [examples/02-hello-ts](../../examples/02-hello-ts) | 稳定 |
| `omui.example.counter` | [examples/03-counter](../../examples/03-counter) | 稳定 |
| `omui.example.rating` | [examples/04-rating](../../examples/04-rating) | 稳定 |
| `omui.example.contrast` | [examples/05-contrast](../../examples/05-contrast) | 稳定 |
| `omui.example.theming` | [examples/06-theming](../../examples/06-theming) | 稳定 |
| `omui.example.headless-and-agent` | [examples/07-headless-and-agent](../../examples/07-headless-and-agent) | 稳定 |
| `omui.example.markdown-viewer` | [examples/08-markdown-viewer](../../examples/08-markdown-viewer) | 稳定 |
| `omui.example.markdown-stream` | [examples/09-markdown-stream](../../examples/09-markdown-stream) | 稳定 |
| `omui.example.canvas-basics` | [examples/10-canvas-basics](../../examples/10-canvas-basics) | 稳定 |
| `omui.example.notes` | [examples/12-notes](../../examples/12-notes) | 稳定 |
| `omui.example.live-design` | [examples/13-live-design](../../examples/13-live-design) | 稳定 |

### 构建插件（mcpp）

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.build` | [mcpp/build](../../mcpp/build) | 稳定 |

### 作品（showcase）

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.showcase.fighting` | [showcase/fighting](../../showcase/fighting) | 实验 |
| `omui.showcase.fighting.headless` | [showcase/fighting/headless](../../showcase/fighting/headless) | 实验 |
| `omui.showcase.markdown_wall` | [showcase/markdown-wall](../../showcase/markdown-wall) | 预览 |
| `omui.showcase.promo_video` | [showcase/promo-video](../../showcase/promo-video) | 预览 |
| `omui.showcase.style_gallery` | [showcase/style-gallery](../../showcase/style-gallery) | 预览 |
| `omui.showcase.widget_breakout` | [showcase/widget-breakout](../../showcase/widget-breakout) | 预览 |
| `omui.showcase.widget_breakout.headless` | [showcase/widget-breakout/headless](../../showcase/widget-breakout/headless) | 预览 |
| `omui.showcase.kiln` | [showcase/kiln](../../showcase/kiln) | 预览 |
| `omui.showcase.kiln.window` | [showcase/kiln/window](../../showcase/kiln/window) | 预览 |
| `omui.showcase.quay` | [showcase/quay](../../showcase/quay) | 预览 |
| `omui.showcase.quay.play` | [showcase/quay/play](../../showcase/quay/play) | 预览 |
| `omui.showcase.quay.window` | [showcase/quay/window](../../showcase/quay/window) | 预览 |
| `omui.showcase.glade.headless` | [showcase/glade/headless](../../showcase/glade/headless) | 实验 |
| `omui.showcase.swarm.headless` | [showcase/swarm/headless](../../showcase/swarm/headless) | 实验 |
| `omui.showcase.swarm.window` | [showcase/swarm/window](../../showcase/swarm/window) | 实验 |

### 官网

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.site` | [site](../../site) | 预览 |

### 仓库测试

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.bench` | [tests/bench](../../tests/bench) | 稳定 |
| `omui.bench.competitors` | [tests/bench/competitors](../../tests/bench/competitors) | 稳定 |
| `omui.bench.competitors.apps` | [tests/bench/competitors/apps](../../tests/bench/competitors/apps) | 稳定 |
| `omui.size.terminal` | [tests/size/terminal](../../tests/size/terminal) | 预览 |
| `omui.size.terminal-agent` | [tests/size/terminal-agent](../../tests/size/terminal-agent) | 预览 |
| `omui.size.yard` | [tests/size/yard](../../tests/size/yard) | 稳定 |
| `omui.features` | [tests/features](../../tests/features) | 稳定 |
| `acme.plugins` | [tests/plugins](../../tests/plugins) | 稳定 |

### 仓库工具

| 包 | 目录 | 标注 |
|---|---|---|
| `omui.devtools` | [tools/devtools](../../tools/devtools) | 稳定 |

由 `omui-devtools docs` 从每个包的 manifest 生成，不要手改。
