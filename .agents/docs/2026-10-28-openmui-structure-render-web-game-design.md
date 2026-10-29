# openmui：目录与命名、统一渲染、Web 与 Toy、游戏引擎与格斗框架、媒体

> 状态：草案 v1.0（2026-10-28），待 review
> 依据：[architecture](2026-10-26-openmui-architecture-design.md)（D-55、D-56、D-58）、[特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md)（D-63、D-65、D-66、D-68）、[渐进式披露](2026-10-28-openmui-progressive-disclosure-design.md)（D-70）、[i18n / 窗口 / 引擎分层](2026-10-28-openmui-i18n-window-engine-design.md)（D-75、D-76、D-77）、[实施计划](2026-10-26-openmui-implementation-plan.md)（§13、§18、§19）
> 新决策：D-80 到 D-86；待决问题 Q-20 到 Q-29

本文回答五个问题：仓库的目录和名字怎样做到一眼能懂、以后能拆；2D 和 3D 怎样共用一套渲染架构；openmui 怎样上 Web 和 B 站 Toy，官网怎样用框架自己做；游戏引擎和格斗框架怎样设计，才能从 2D 演示走到高质量 3D，而只用 openmui 的人不会被它们拖累；视频、音频和以后的创作软件怎样接进来。

名字现在都是**内部代号**：游戏引擎 `mgame`、格斗框架 `gongfu`、动画引擎 `hanim`、格斗游戏暂叫“格斗游戏”。对外品牌以后再定（Q-20）；本文的架构不依赖这些名字。

---

## 0. 摘要

| 决策 | 一句话 |
|---|---|
| **D-80 目录与命名** | 路径就是包名；`modules/` 按层分组（base / gfx / content / media / ui），宿主移入 `modules/ui/host`；按角色命名（`shader` 不叫 `slang`）；动画引擎叫 `hanim`；每个分组目录有 README；主 README 以作品为中心重写 |
| **D-81 统一渲染** | rhi → shader → render（渲染图）→ 渲染特性（vector / sprite / mesh / particle / effect）；2D 和 3D 是同一套架构里的不同特性；rhi 不再限于“UI 规模” |
| **D-82 产品分层** | 基础组件、openmui、mgame、gongfu、作品五类；依赖只向下；openmui 是独立的 UI 框架，mgame 把它当 UI 模块引用；用多少带多少，体积进预算 |
| **D-83 第三方库** | `compat.<库>` + `mcpplibs.mwrap.<库>`，只在 `native/` 分区 import；没有 GPL；LGPL 只能可选、动态链接；许可由 lint 从依赖图检查 |
| **D-84 Web 与 Toy** | wasm32-emscripten 单线程；`host.web` 带语义 DOM 镜像；WebGPU 为主、WebGL2 兼容、CPU 兜底；`host.web.toy` 把 Toy SDK 映射成 openas 的动作；官网是创意设计、可静态部署到 GitHub Pages |
| **D-85 mgame 与 gongfu** | mgame 是引擎服务（世界、模拟、输入、物理、网络、资源）；gongfu 是格斗框架：定点数 60Hz 确定性模拟、回滚、数值系统、数据驱动的角色，模拟与画面分离，2D 先验证、3D 用同一核心 |
| **D-86 媒体** | `media.audio`（实时音频图，离线渲染逐位确定）、`media.video`（呈现换成编码）、`media.timeline`（剪辑、编曲、动画编辑共用）、撤销和事务 |

---

## 1. 背景

到 2026-10-28，仓库有约 60 个成员，三个平台、两种编译器的 CI 全绿（实施计划 §22）。接下来要做的几件事改变了对结构的要求：

- **Web**：同一份代码编译到 wasm，跑在浏览器、GitHub Pages 和 B 站 Toy 的 WebView 里；
- **游戏**：S4 打砖块，以及一个严肃的格斗框架和格斗游戏，以后还要做高质量 3D；
- **媒体**：S5 宣传片需要视频导出和音频混合，以后可能有 AI 原生的剪辑软件和编曲软件；
- **独立组件**：动画引擎、文字、渲染都应该能被 openmui 以外的项目单独使用。

现有结构的问题：

1. `modules/` 平铺 20 个包，GPU 一族（rhi、rhi-sdl、rhi-cts、slang、effect、render）没有归在一起；
2. 顶层有 17 个目录，其中 `i18n/`、`natives/`、`themes/` 各只有一到三个包，`hosts/`、`runtime/` 和 `modules/` 的边界要靠记忆；
3. 部分名字按实现而不是按角色起（`slang`），或者和以后的概念冲突（`record` 和音视频录制）；
4. 渲染层是为 2D UI 写的，“rhi 范围严格控制在 UI 需要的程度”（实施计划 §20）挡住了 3D；
5. 没有游戏引擎和 UI 框架的边界，也就没有办法保证“只用 openmui 的人不带上游戏引擎”。

---

## 2. 产品分层（D-82）

### 2.1 五类东西

| 类 | 是什么 | 例子 | 可以依赖 |
|---|---|---|---|
| **基础组件** | 不知道 UI、也不知道游戏的库，各自可以单独使用和发布 | hanim、geom、text、layout、rhi、shader、render、audio | 标准库、第三方库、更低层的基础组件 |
| **openmui** | 独立的 UI 框架：规范的实现、运行时、宿主、组件、语言绑定、工具 | core、view、semantic、agent、host.sdl3、ui.base | 基础组件、spec |
| **mgame** | 游戏引擎服务；以 openmui 作为它的 UI 模块 | mgame.world、mgame.sim、mgame.input | 基础组件、openmui |
| **gongfu** | 格斗框架；核心只依赖标准库 | gongfu、gongfu.studio | 核心：标准库；外围：mgame、openmui |
| **作品** | 真实可用的工具、内容、游戏（D-63） | 文档墙、风格工坊、官网、格斗游戏 | 上面全部 |

```
作品（showcase/、site/、examples/）
   ↑
gongfu 外围（画面层、Studio、训练模式 UI）      gongfu 核心（只依赖标准库）
   ↑
mgame（世界、模拟、输入、物理、网络、资源、ui 桥）
   ↑
openmui（spec ← core ← view ← … ← host、components、lang、tools）
   ↑
基础组件：base ← gfx ← content ← media
```

规则（lint `layering` 按分组目录检查）：

1. **依赖只向下。** openmui 永远不依赖 mgame 或 gongfu；基础组件永远不依赖 openmui。
2. **mgame 引用 openmui**：游戏的菜单、HUD、训练模式、设置界面都是 openmui 视图；输入先给 UI，UI 没有消耗的再给游戏（D-76 的输入路由）。
3. **gongfu 核心只依赖标准库**（§8.2），这是为了以后可以原样拆出去，甚至做成二进制库：lint 单独检查它。

### 2.2 用多少带多少

mcpp 只构建声明过的依赖，所以“不带上游戏引擎”首先由包的粒度保证：

- 伞包 `openmui`（`import openmui;`）默认只包含 UI 需要的部分；`media.video`、`media.audio`、`render.mesh` 等通过 feature 打开；
- mgame 有自己的伞包，不被 openmui 的伞包引用；
- **可以检查**：hello 应用（桌面和 wasm）的二进制体积写进 `budgets.bench.omui.json`，超过即失败；lint 检查 hello 的依赖闭包里没有 `mgame.*`、`gongfu*`、`media.video`。

### 2.3 gongfu 以后可以拆出去

gongfu 现在放在本仓库，作为一组模块开发。为了以后拆成独立仓库（甚至私有仓库 + 二进制 SDK）只是机械工作，现在就遵守三条：

1. 核心只依赖标准库（不用 openmui.expr、hanim、geom，定点数和表达式自己实现，§8.3）；
2. 对外接口粗粒度、以数据交换：每帧调用几次（推进一帧、存档、读档、校验和、取画面状态），以后包一层 C ABI 即可；
3. 画面、音频、输入设备、网络传输、UI 都在核心之外。

---

## 3. 目录与命名（D-80）

### 3.1 命名规则

1. **按角色命名，不按实现命名**：`text` 而不是 harfbuzz，`shader` 而不是 slang。只有绑定某个具体实现的后端和宿主用实现名：`rhi.sdl`、`rhi.webgpu`、`host.sdl3`。
2. **用业界通用的词**：render graph、material、mesh、sprite、skeleton、mixer、timeline、codec、session。
3. **路径就是包名**：去掉 `openmui.` 前缀，最后一段是叶子目录名（下划线写成连字符）；子包放在父包目录里（`rhi.sdl` 在 `rhi/sdl/`，已有先例 `slang/rules`）。分组目录（`base/`、`gfx/`……）只做归类，不进包名。
4. **缩写只保留通用的**：ui、rhi、i18n、geom、gfx；其余写全。
5. **包名和数据文件的种类同名**：`session` ↔ `*.session.omui.jsonl`，`theme` ↔ `*.theme.omui.toml`。
6. **有名字的组件用自己的名字**：动画引擎 `hanim`（模块 `import hanim;`，命名空间 `hanim::`，包 `hanim`）；mgame、gongfu 同理（`import mgame.world;`、`import gongfu;`）。其他独立组件先用通用名，在 `openmui.` 下（Q-20）。

### 3.2 目录树

```
mui/
├── spec/                       标准（openlifa/openmui 子模块）
├── modules/                    README：分层图和依赖规则
│   ├── base/                   基础：无 UI 概念
│   │   ├── hanim/              ← modules/anim（openmui.anim → hanim）
│   │   ├── geom/ memory/ unicode/ fuzzy/ trace/ pipeline/ image/
│   │   ├── session/            ← modules/record（openmui.record → openmui.session）
│   │   └── testing/            ← tests/support（openmui.testing）
│   ├── gfx/                    图形
│   │   ├── rhi/                rhi + sdl/ cts/（← rhi-sdl、rhi-cts）+ webgpu/ webgl/（新）
│   │   ├── shader/             ← modules/slang（openmui.slang → openmui.shader）+ rules/
│   │   ├── raster/
│   │   └── render/             render（渲染图，D-81）+ vector/（现在的 2D 渲染器）+ effect/（← modules/effect）
│   │                           + sprite/ mesh/ particle/（新，按需要加入）
│   ├── content/                文字和内容
│   │   ├── text/ layout/ markdown/
│   │   └── i18n/               + cldr/（← i18n/cldr）
│   ├── media/                  媒体（新）：audio/ video/ timeline/
│   └── ui/                     openmui
│       ├── core/ view/ semantic/ agent/ expr/ viewdoc/ abi/ player/ reflect/   ← runtime/*
│       ├── theme/              + default/ compact/ styles/（← themes/*）
│       ├── native/             + fuzzy/（← natives/fuzzy）
│       └── host/               headless/ sdl3/（← hosts/*）+ web/ + web/toy/（新）
├── mgame/                      游戏引擎（新）：world/ sim/ input/ physics/ net/ assets/ ui/
│   └── gongfu/                 格斗框架（新）：核心 + present2d/ present3d/ studio/
├── components/                 openmui.ui.*：base/ canvas/ canvas/kit/（← canvas-kit）markdown/ window/ liquid-glass/
├── lang/                       common/ js/ lua/ python/
├── tools/                      gen/ lint/ ci/ cli/（← tools/omui）mcp/ inspector/
├── tests/                      cts/ rules/ bench/ features/
├── examples/                   01–12
├── showcase/                   markdown-wall/ style-gallery/ fighting/（新）
├── site/                       官网（新，D-84）
├── templates/  docs/  src/  mcpp/
```

顶层从 17 个目录变成 14 个，每个目录回答一个问题：

| 目录 | 回答的问题 |
|---|---|
| `spec/` | 规范是什么 |
| `modules/` | 框架由什么组成（库） |
| `mgame/` | 游戏引擎和格斗框架 |
| `components/`、`lang/`、`tools/` | 使用者直接用到的组件、语言绑定、工具 |
| `examples/`、`showcase/`、`site/` | 用框架做出来的东西 |
| `tests/`、`templates/`、`docs/`、`src/`、`mcpp/` | 跨成员的测试、模板、文档、伞包入口、构建代码 |

`modules/` 内部的层次（依赖只向下）：`base` ← `gfx` ← `content` ← `media`，它们是“基础组件”（D-55 的独立性只约束这四组）；`ui` 在 `spec` 之上，是 openmui。

注：`text` 依赖 `render`（字形进显示列表），所以 `content` 在 `gfx` 之上；`image` 被 `render` 和 `text` 使用，所以放在 `base`。

### 3.3 改名表

| 现在 | 之后 | 原因 |
|---|---|---|
| `openmui.anim`（`modules/anim`） | `hanim`（`modules/base/hanim`） | 有名字的组件（规则 6） |
| `openmui.slang` / `openmui.slang.rules` | `openmui.shader` / `openmui.shader.rules` | 按角色（规则 1） |
| `openmui.record` | `openmui.session` | 和数据文件同名；避免和音视频录制混淆 |
| `openmui.effect` | `openmui.render.effect` | 效果是渲染图里的一类 pass（D-81） |
| `openmui.render` | `openmui.render`（渲染图）+ `openmui.render.vector`（2D 矢量） | D-81；拆分在 D-81 那一轮做 |
| `rhi-sdl`、`rhi-cts` 目录 | `rhi/sdl`、`rhi/cts` | 路径就是包名 |
| `components/canvas-kit`（`openmui.ui.canvas_kit`） | `components/canvas/kit`（`openmui.ui.canvas.kit`） | 子包 |
| `tools/omui`（`openmui.cli`） | `tools/cli` | 路径就是包名；命令行仍叫 `omui` |
| `openmui.pipeline` | 不变 | rhi 里的 GPU 管线对象改叫 `PipelineState`，避免两个“pipeline” |

C++ 命名空间：`omui::anim` → `hanim::`；其他不变（`omui::`）。

### 3.4 分组目录的 README

每个分组目录（`modules/`、`modules/base/`、`modules/gfx/`、`modules/content/`、`modules/media/`、`modules/ui/`、`modules/ui/host/`、`mgame/`、`components/`、`lang/`、`tools/`、`tests/`、`examples/`、`showcase/`）放一份短 README，固定四段：

```markdown
# modules/gfx

图形：GPU 抽象、着色器、CPU 光栅器和渲染器。不知道界面，也不知道游戏。

依赖：`modules/base`。被使用：`content`、`media`、`ui`、`mgame`。

| 包 | 用途 |
|---|---|
| [rhi](rhi) | GPU 抽象：描述先校验、命令列表是数据；后端 sdl、webgpu、webgl |
| …… | |

从 [render](render/README.md) 开始读。
```

- 只写已经实现的东西（mcpp 的 `mcpp-docs-style`）；
- 细节链接到成员自己的 README，不重复；
- **lint `layout`**：检查每个分组目录有 README、表里的包和实际的子包一一对应、每个成员的路径符合规则 3。

### 3.5 主 README

英文 `README.md` 为主，`README.zh.md` 结构相同。原则：简洁、只写已实现的、**以作品为中心**——每个作品是真实可用的工具，同时展示框架能力（D-63）；配图由 headless 渲染、CI 校验哈希（配图本身就是确定性的证明）。

````markdown
<div align="center">

# openmui

**One semantic tree — rendered as pixels, accessibility, agents and tests.**

[CI] [Apache-2.0] [openmui 0.3] [openas 0.2]

[中文](README.zh.md) · [Features](docs/en/features.md) · [Benchmarks](docs/en/benchmarks/README.md) · [Design](.agents/docs/README.md)

<img src="docs/assets/works.png" alt="The wall, the styles, an agent at work">

</div>

## Works

Each one is a tool you can use, and each one shows what the framework does.

| | What it is | What it shows |
|---|---|---|
| **[Wall](showcase/markdown-wall)** | A wall of documents on an infinite canvas: read a whole folder of Markdown at once | 1000 windows zoomed 1%–400% at p95 0.12 ms; live near, textures far; declarative behaviours |
| **[Styles](showcase/style-gallery)** | A settings panel and dashboard in five style packs | Slang effects, cross-fades, one semantic tree in every style |
| **[Markdown](examples/08-markdown-viewer)** | Read documents; pipe a model's output in and watch it set as it arrives | CommonMark 652/652, incremental parsing, glyph fade-in |
| **[Space](examples/10-canvas-basics)** | Where your disk went, as a map you can zoom | Canvas, treemap, agents that find by name |
| **[Notes](examples/12-notes)** | Notes written in TypeScript, Lua, a view document and C++ at once | One tree, many languages: signals, verbs, native modules |
| **[Contrast](examples/05-contrast)** | A contrast checker for accessible colours | Native-module contracts, generated bindings |
| **[Inspector](tools/inspector) · [omui](tools/cli) · [MCP](tools/mcp)** | See and drive any running application | One record, two interfaces; grants under the developer's cap |

## Hello

```cpp
import openmui;

omui::AppSpec app() {
    static int clicks = 0;
    return {.title = "Hello", .view = [] {
        return omui::immediate([](omui::Ui& ui) {
            if (ui.button(std::format("Clicked {} times", clicks), {.id = "click"})) ++clicks;
        });
    }};
}
```

```sh
mcpp new hello --template openmui && cd hello && mcpp run -- --serve
omui agent act org.example.hello main/click activate     # an agent presses the same button
```

## Standards

openmui implements two OpenLifa standards: [openmui](https://github.com/openlifa/openmui) (interfaces) and [openas](https://github.com/openlifa/openas) (actors, effects, grants).

<sub>In development · Code Apache-2.0 · Specification CC BY-SA 4.0</sub>
````

Web 版、官网、格斗游戏、视频导出做出来以后，各加一行作品。

### 3.6 迁移

分两轮，每轮全平台 CI 通过：

1. **搬目录**：`git mv`（保留历史），改所有 `mcpp.toml` 的 `path`、workspace 成员表、CI 脚本、lint 的层表、`mcpp.lock`、模板和文档里的路径；包名不变；加分组 README、新主 README、lint `layout`。
2. **改名**：`hanim`、`shader`、`session`、`render.effect`、`ui.canvas.kit`；所有 `import` 和命名空间；生成器和 registry 里引用的名字；文档。

同时修订：AGENTS.md（`modules/` 的定义、动画引擎的规则、依赖方向、第三方库和许可规则）、实施计划的目录树和 §20 的 rhi 风险、架构文档的 D-55 / D-58 / D-76。

---

## 4. 统一渲染架构（D-81）

### 4.1 层次

```
rhi              设备抽象：缓冲、纹理、采样器、PipelineState、命令列表（数据）、深度/模板、MSAA、compute、时间戳查询
 │               后端：rhi.sdl（SDL_GPU：Vulkan / Metal / D3D12）、rhi.webgpu、rhi.webgl（WebGL2）；rhi.cts 一致性
 └─ shader       Slang：模块、接口和泛型做材质；按后端输出 SPIR-V / MSL / DXIL / WGSL / GLSL ES；反射出绑定；构建时编译
     └─ render   渲染图：pass、临时资源池、load/store、按需裁剪 pass、每个 pass 一个 trace span；视图和相机；材质；渲染特性注册
         ├─ render.vector     2D 矢量（现在的显示列表 + CPU 参考 + GPU 实例化四边形）
         ├─ render.sprite     精灵批处理、图集、2D 骨骼蒙皮、调色板
         ├─ render.mesh       3D：蒙皮网格、PBR 与卡通着色、阴影、描边
         ├─ render.particle   粒子：（发射时刻, 种子, 时间）的纯函数
         └─ render.effect     后处理与像素效果：现有 Slang 效果、玻璃、泛光、色调映射、调色
```

- **2D 和 3D 只是不同的渲染特性**，共用 rhi、shader、材质、资源缓存、渲染图、trace 和测试方式。
- **SDL_GPU 和 WebGPU 都自动管理资源状态**，渲染图不需要自己算屏障；它负责 pass 的顺序和裁剪、临时纹理复用、按 pass 计时。WebGL2 后端在 rhi 内部处理它需要的状态。

### 4.2 渲染图和帧管线的关系

D-66 的帧管线决定“一帧有哪些阶段”（输入 → 动画 → 布局 → 绘制 → damage → 渲染 → 呈现）；`render` 阶段内部跑渲染图。两层用同一种描述方式：带类型的端口、构建时检查（每个资源有生产者、类型一致、无环）、每个节点自动 trace。

- 视频导出：帧管线把“呈现”换成“编码”（D-86）；
- 音频图（D-86）是同一种有向无环图，不同的是它在音频线程上按块运行。

### 4.3 组合方式

| 组合 | 怎么做 |
|---|---|
| UI 叠在 3D 上 | 世界 pass → UI pass（vector）→ 合成 |
| 3D 嵌在 UI 里 | 组件 `ui.viewport`：一个节点持有一个视图，渲染图为它加一个 pass，结果作为图层纹理 |
| UI 进 3D 世界 | UI 渲染到纹理（`OMUI_EXT_render_to_texture`，D-76），由引擎决定怎样合成 |

### 4.4 确定性和一致性

- `render.vector` 保留 CPU 参考实现，金图在所有平台逐字节相同（现状不变）；GPU 路径和 CPU 参考的差别有上限（现状：不超过一级）。
- 3D 特性没有 CPU 参考；金图在 lavapipe 上固定，真实 GPU 只做容差比较。
- 每个渲染特性有 CTS（D-65：每个插件种类要有 CTS）。

### 4.5 迁移

1. 现有 `openmui.render` 拆成 `render`（渲染图、渲染器接口、资源）和 `render.vector`（显示列表、CPU、GPU 2D），**像素哈希一律不变**；
2. rhi 扩展深度/模板、MSAA、compute、时间戳查询，每项进 rhi.cts；
3. `render.sprite` 随 S4 和格斗游戏加入，`render.mesh` 随 3D 加入。

修订：实施计划 §20 的“GPU 抽象层的范围严格控制在 UI 需要的程度”改为“范围由渲染特性的需要决定，每项能力进 rhi.cts”。

---

## 5. 第三方库与许可（D-83）

### 5.1 引入方式（mwrap 规范）

- 每个库有两个包：`compat.<库>`（从源码构建，或官方二进制 SDK）和 `mcpplibs.mwrap.<库>`（把 C/C++ 接口包装成模块，宏映射成 constexpr）；
- openmui 里只在 `native/` 实现分区 `import mcpplibs.mwrap.<库>;`；
- 上游先做：在 mcpp-index 和 mcpplibs 里加好、CI 通过、发布以后再在本仓库使用（不推迟、不绕开）；
- **lint `thirdparty`**：第三方模块只能出现在 `native/` 分区；第三方头文件只出现在 mwrap 包里。

### 5.2 许可

| 许可 | 规则 |
|---|---|
| MIT、BSD、Apache-2.0、Zlib、BSL-1.0、MIT-0、公有领域 | 直接使用 |
| MPL-2.0 | 可以；不修改它的文件（MPL 只要求公开被修改的文件，不传染到应用） |
| LGPL | 只能是**可选**插件，**动态链接**，可以被替换；默认不带 |
| GPL、AGPL | **不进入进程**，动态链接也不行。唯一的方式是另起进程，调用用户自己安装的程序（例如用户自己装的 ffmpeg 命令行）；我们不分发 |
| 专有但免费的 SDK（Steamworks） | 只作为可选的宿主插件 |

- **lint `licenses`**：读取依赖图里每个包的 `licenses` 字段（mcpp-index 的描述里都有），遇到 GPL / AGPL 失败；LGPL 必须标为可选且动态链接。
- **专利**：默认免版税格式（AV1、VP9、Opus、PNG、WebP）；H.264 / HEVC 只走系统编码器（Media Foundation、VideoToolbox、VA-API）或浏览器的 WebCodecs。

### 5.3 选库标准

高质量（广泛使用、维护活跃、有测试）、跨平台（含 wasm）、**依赖少**、高性能、许可符合 §5.2。同等条件下选单文件或零依赖的库。

### 5.4 候选库

| 用途 | 库 | 许可 | 依赖 | 索引里 | 什么时候引入 |
|---|---|---|---|---|---|
| WebGL2 的着色器 | SPIRV-Cross | Apache-2.0 | 无 | 没有 | `rhi.webgl`（如果 Slang 的 GLSL ES 输出不够，Q-21） |
| 压缩纹理 | basis_universal（Web 只需转码器） | Apache-2.0 | 无 | 没有 | Web 和格斗游戏的资源 |
| glTF | cgltf 或 fastgltf | MIT | 无 | 没有 | `render.mesh` |
| 网格优化 | meshoptimizer | MIT | 无 | 没有 | `render.mesh` |
| 3D 骨骼动画 | ozz-animation | MIT | 无 | 没有 | hanim 的 3D 骨骼（或参考其设计自写，Q-26） |
| 2D 物理 | Box2D v3 | MIT | 无 | 没有 | S4（`mgame.physics`） |
| 3D 物理 | Jolt Physics | MIT | 无 | 没有 | 以后 |
| 音频设备 | miniaudio（含 Web Audio 后端） | MIT-0 / 公有领域 | 无 | 有 compat，缺 mwrap | `media.audio` |
| 音频解码 | libopus + opusfile、dr_libs（wav / flac / mp3） | BSD / 公有领域 | 少 | 没有 | `media.audio` |
| 网络 | ENet（桌面 UDP）、libdatachannel（桌面 WebRTC，和网页互通） | MIT / MPL-2.0 | 无 / 少 | 没有 | `mgame.net`（Web 用浏览器自带的 WebRTC） |
| 视频 | ffmpeg（LGPL 构建，可选）、dav1d、SVT-AV1 | LGPL / BSD | | ffmpeg 有 | `media.video` |
| 容器 | libwebm、minimp4 | BSD / CC0 | 无 | 没有 | `media.video`（不带 ffmpeg 时） |
| 音乐软件 | CLAP、VST3 SDK（3.8 起 MIT）、libremidi | MIT / MIT / BSD | 无 | 没有 | 以后（编曲软件） |
| 大字号文字 | msdfgen | MIT | 少 | 有 compat | 游戏里的大字和 3D 文字 |
| ECS | EnTT / flecs，或自写列存储 | MIT | 无 | 没有 | `mgame.world`（Q-25） |

不选：assimp（太大、依赖多）、Spine 运行时（需要授权）、GPL 的编码器（x264、x265）。

---

## 6. Web、Toy 与官网（D-84）

**现状（2026-10-29）**：

- 已有：`host.web`（canvas、requestAnimationFrame、按需出帧、指针/触摸/滚轮/键盘、Gamepad API 标准手柄、CPU 渲染贴到 canvas、页面可见性即窗口焦点）；`omui::run` 在 wasm 上自动用它（伞包的 `openmui:host` 分区由清单按目标选择）；`host.web.toy`（isSupport、云存储按检查点批量写、排行榜、错误映射到 openas，307044 在页面里退避重试）；`dist-toy` 打包格式（`mcpp pack --pkg-format dist-toy`）；`tools/ci/toy.sh` 打包格斗游戏和打砖块并用固定版本的 `toy_doctor.py` 检查，两者都通过、无警告。
- 体积（release）：index.html 2.4 KB；格斗 13.9 MiB（wasm 8.8 MiB、字体 4.8 MiB），打砖块 14.5 MiB；zip 后 6.9 / 6.2 MiB。字体是大头，子集化（§6.4）是下一步。
- 发现：Toy 没有清单文件（标题、slug、封面是 `toy create` 的参数），打包时在包旁边写 `<target>.toy.omui.json`；SDK 由页面自己用 script 标签加载；index 包外的上限按账号而定。mcpp-index 里有些包的 `linux` 链接参数（taskflow 的 `-pthread`、harfbuzz/libwebp 的 `-lpthread`）也落到 emscripten 行，用 `-sUSE_PTHREADS=0` 抵消；QuickJS-ng 的 mwrap 只支持 64 位，Web 上 `lang.js` 暂时回答“不可用”。
- 已有（2026-10-30，T4）：输入法的隐藏 textarea（组合事件接到 `PreeditEvent` / `TextEvent`）；剪贴板（页面最后知道的内容，写入走 Async Clipboard API）；语义 DOM 镜像（`DomMirror`：ARIA 角色、名字、值、状态、焦点、框，只发变化，polite / assertive 两个 live region，读屏的焦点和激活回到运行时）；`window.omui`（openmui.tree@0.1，经 `openas::decide`，不可信的 DOM 事件不算人的输入）；`Store` 插件（新包 `openmui.native.store`，Web 上是 localStorage，键在 `omui/<app>/` 下）。Web 行跑 host.web 的 wasm 测试、页面一半在模拟 DOM 下的测试，以及 wasm 程序放进模拟页面的端到端测试。
- 已有（2026-10-30，T6 前半）：`rhi.webgpu`（`omui.rhi` 在页面的 WebGPU 上；Slang 经 `openmui.shader` 输出 WGSL，绑定改写成 WebGPU 的布局；一次提交一次调用，命令列表按 `openmui.rhi:wire` 编成 32 位字）；rhi.cts 的全部用例在 headless Chromium 的 WebGPU（SwiftShader）上通过，Web 构建写出的每个 WGSL 入口都经浏览器编译（`tools/ci/browser.sh`）。回读要程序能挂起（`-sASYNCIFY`；JSPI 和异常的 `invoke_*` 冲突），呈现不需要。
- 还没有：WebGL2、`host.web` 用 GPU 画（现在仍是 CPU 渲染器）、IndexedDB（比 localStorage 大的存储）、Toy 的 navigate/profile/容器状态；脚本（Lua/Python/JS）在 Web 上需要单线程执行（`lang::Host` 现在起线程）；core/view 的依赖链还没有声明 `platforms`，host.web 在 Web 行里是显式加上的。

### 6.1 构建

- 目标 `wasm32-emscripten`（mcpp 已支持；emsdk 由 xlings 安装，版本由 `.xlings.json` 固定）；
- **单线程**：GitHub Pages 和 Toy 都不能设置 COOP/COEP 响应头，没有 SharedArrayBuffer。任务调度做成插件，Web 上用单线程执行器（运行时已有确定性模式下“下一帧等待后台任务”的路径）；
- 不进 Web 的：Python 绑定、dlopen、运行时编译 Slang（着色器在构建时编译）、窗口外框和输入穿透、进程和 IPC；
- **CI 加一行 wasm**：用 node 跑 headless 测试，**像素哈希必须和桌面相同**（wasm 浮点严格按 IEEE、默认不收缩，要在 W1 实测确认）；同时测出体积，写进预算。

### 6.2 `host.web`

| 能力 | 做法 |
|---|---|
| 画布 | `<canvas>`，设备像素比，尺寸变化 |
| 帧节奏 | requestAnimationFrame；FrameDemand（D-70）空闲时不请求下一帧 |
| 输入 | 指针、触摸、键盘、滚轮、Gamepad API |
| 输入法 | 光标处一个隐藏的 textarea，组合事件接到现有的输入法流程 |
| 剪贴板 | Async Clipboard API |
| 无障碍 | **语义 DOM 镜像**：每批变化后把语义树同步成隐藏的 DOM（registry 的 role → ARIA role、焦点、live region） |
| agent | 页面里的 `window.omui`（snapshot / find / act / invoke / wait），经过 `openas::decide`；人的在场只由真实的 DOM 输入事件决定（openas §2.7） |
| 存储 | `Store` 插件：localStorage / IndexedDB |
| 平台适配 | 子包 `host.web.toy`（§6.5） |

### 6.3 GPU

- `rhi.webgpu`：主路径；Slang 输出 WGSL；emscripten 的 WebGPU 接口；
- `rhi.webgl`（WebGL2）：兼容路径。格斗游戏要全屏 60 帧加特效，B 站 App 的 WebView 不一定有 WebGPU（Q-22），所以 Toy 需要它；
- CPU 渲染器：最后的兜底，结果画到 canvas 上；输出和 CI 金图逐字节相同。

### 6.4 体积

- `web-lite` 构建配置：用不到就不带 Lua、QuickJS、表达式、Markdown；
- 字体按用到的字生成子集（内容在构建时已知，CJK 字体不必整套带上）；
- 纹理用 Basis / KTX2，音频用 Opus；
- 文件名带内容指纹；
- 体积预算进 bench，作为门禁；具体数字在 W1 测出后写入（Q-23）。

### 6.5 B 站 Toy

Toy 是 H5 包：运行在 `https://www.bilibili.com/toy/<slug>/` 子路径下，入口 `index.html` 解压后最大 1MB（硬性），资源必须用相对路径，路由用 hash；`window.toy` 提供平台能力（[bilibili/toy](https://github.com/bilibili/toy) 的内容清单和 SDK 文档）。

**能力映射**（`host.web.toy`）：

| Toy SDK | openmui 里是什么 | openas |
|---|---|---|
| `isSupport` | 能力表：不支持的能力不注册对应的动作，agent 看到的列表永远准确 | — |
| `navigate`（要求用户手势） | 动作 `toy.navigate`，只在人亲手操作的输入事件里运行 | external；在场（§2.7） |
| `getUserProfile`（平台弹窗确认） | 动作 `toy.profile`，结果是一个 signal | external；在场 |
| 云存储、共享存档 | `Store` 插件 `store.toy`：先写内存，在关键节点（页面隐藏、显式 `flush`）批量落盘；键名、128 个键、1KB 值的限制在写入时检查 | reversible |
| `submitScore` / `getRankList` / `getMyRank` | `Leaderboard` 服务接口 + 契约优先的 `ui.leaderboard` 组件；headless 测试用模拟实现；只在结算时提交 | external / read |
| 作者资料、视频、关系 | 只读动作 `toy.author.*`，结果是 signal | read |
| `saveImageToAlbum` | 动作，图片由框架自己渲染（和 headless 一致）；base64 超过 5MB 报错、超过 2MB 提示 | external |
| `share` / `getQrCode` | 动作；路径必须相对、不能越界，写入时检查 | external / read |
| `closeBrowser` | 窗口的 `request_close` | — |
| 容器状态 | 窗口信号：可用尺寸、**安全区**（新增）、方向、沉浸、设备类型；`setContainerMode` 是动作，**是否生效以信号变化为准**（Promise 不是回执） | reversible |
| `checkInputSafety` | 文本框的异步校验：ok / rejected / unavailable 三态，unavailable 不等于通过 | read |
| `requestCamera` / `requestMicrophone` | 以后（媒体输入） | external；在场 |

**错误映射**：`invalid_param` → `invalid-argument`；`unsupported` → `refused`；限流 `307044` → **退出码 3（not ready：同一个请求以后可能成功）**，`Store` 和 `Leaderboard` 自动指数退避；其他 → `failed`。openas 0.2 的退出码 3 正好对应 Toy 的限流语义。

**打包**：`dist-toy` 插件生成 `dist/toy/`：很小的 `index.html`、`<名字>-<哈希>.js / .wasm`、`assets/<哈希>.*`，全部相对路径；CI 运行 Toy 官方的 `toy_doctor.py`（MIT，零依赖，固定版本）检查。**发布不在本仓库的范围内**，由维护者自己用 `toy` 命令行完成。

小红书「小工具」禁止 WASM、Worker 和网络请求（[容器能力清单](https://miniapp-sandbox.xiaohongshu.com/minitool/doc)），**不支持**。

### 6.6 官网（`site/`）

**创意方向**：不做常见的产品页（大标题、三张卡片、渐变）。官网本身演示项目的核心命题。

| 设计 | 内容 |
|---|---|
| **一棵树的四种渲染** | 一个“镜头”开关：同一页面从像素连续过渡到语义树线框、agent 看到的 JSON、测试断言 |
| **网站是一块无限画布** | 先看到所有文档和作品的全景，再缩放进某一处（S1 的做法）；是一个个“地方”，不是一页页的页面 |
| **时间轴** | 页面底部一条时间轴：拖动整个页面的动画、倒放；旁边显示这一帧在你浏览器里的哈希，和 CI 固定的值对照 |
| **排版** | 中英混排本身就是展示；切换语言时字形变形过渡 |
| **可运行的作品** | 文档墙、风格工坊、示例；格斗游戏的出招表（逐帧播放招式和判定框，对玩家是真实的帧数据查询站） |
| **agent 面板** | 页面上的 agent 通过总线操作页面；演示 S-11：未授权时被拒绝，人在界面里授权后运行 |

**技术**：

- 应用是 openmui 的 wasm 程序，内容来自 `docs/`（Markdown 由 `ui.markdown` 渲染）；
- **构建时预渲染**：headless 从同一棵语义树为每一页生成静态 HTML（Markdown 写出的 HTML 已和 md4c-html 逐字节相同），搜索引擎和关掉 JS 的访客读 HTML，wasm 加载后接管；
- 部署：mui 是公开仓库，GitHub Actions 用 `actions/deploy-pages` 部署；Pages 和 Toy 一样在子路径下，同一套相对路径规则；
- 满足 M6 ①：S1 的缩小版和示例 01 运行在 Web 上。

---

## 7. mgame：游戏引擎服务（D-85 的一部分）

mgame 是一组引擎服务，以 openmui 作为 UI 模块。第一个用户是 S4 打砖块，第二个是格斗游戏。

| 包 | 内容 |
|---|---|
| `mgame.world` | 实体和按列存储的组件；遍历顺序确定；整块拷贝即存档（回滚需要）；先评估 EnTT / flecs（Q-25） |
| `mgame.sim` | 固定步长的模拟循环，和帧节奏分开；可以回滚重算；模拟时钟和呈现时钟分开 |
| `mgame.input` | 设备（键盘、手柄、触摸）→ 动作映射；每帧采样；输入延迟可测 |
| `mgame.physics` | Box2D v3（S4）；确定性 |
| `mgame.net` | 回滚会话（和传输无关）；传输插件：ENet / libdatachannel / 浏览器 WebRTC |
| `mgame.assets` | 资源包、内容哈希、热重载 |
| `mgame.ui` | openmui 接入：游戏的菜单和 HUD 是 openmui 视图；输入先给 UI |

主循环、时钟、渲染设备、输入都可以注入（D-76）；mgame 用 openmui 的宿主（SDL3、Web），也可以被别的引擎嵌入。

---

## 8. gongfu：格斗框架（D-85）

### 8.1 目标

- **手感对标顶级格斗游戏**：帧精确、输入延迟低、回滚稳定、训练工具完整；
- **2D 是框架验证，3D 用同一个核心**：街霸 6 画面是 3D，玩法和判定在一个 2D 平面上，所以“像街霸 6 的 3D 格斗”和 2D 格斗的模拟完全相同，区别只在画面层；
- **数据驱动**：角色、招式、规则、场景都是数据，格式从第一天起就是以后的 mod 格式（创意工坊以后做，§8.10）。

### 8.2 模拟核心（`gongfu`，只依赖标准库）

| 部分 | 设计 |
|---|---|
| 时间 | 固定 60Hz；一帧是一次 `tick(两名玩家的输入)` |
| 数值 | **定点数**（精度 Q-24）；不用浮点 |
| 状态 | 一块连续的纯数据；存档和读档是一次拷贝；每帧一个校验和（发现不同步） |
| 回滚 | 最多回退 8–10 帧重新模拟；可配置的输入延迟 |
| 判定 | 默认 2D 帧判定框：攻击框、受击框、推挤框、投技框、近身框；以后为真 3D 加挂在骨骼上的判定体 |
| 规则 | 打击停顿、受击 / 防御硬直、推开、版边、投技和拆投、连段补正、反击和惩罚反击、能量和资源槽 |
| 状态机 | 框架定义通用状态（站、蹲、跳、攻击的发生 / 持续 / 收招、受击、倒地、起身）；招式在数据里声明取消窗口；初期不允许角色写任意状态机（保证平衡和 3D 兼容） |
| 输入 | 8 方向 + 6 键（LP MP HP LK MK HK）；搓招识别（236、214、623、41236、360、蓄力、双击冲刺）和每种指令的宽容帧数；输入缓冲；放开按键触发；同时按键窗口；SOCD 策略可配；操作方案：经典、简易（类似 Modern）、触屏 |
| 接口 | 创建、`tick`、`save` / `load`、`checksum`、`view`（这一帧要画什么）、`check`（数据诊断）；粗粒度，以后可以包 C ABI |

### 8.3 数值系统和数据

- **角色属性**：血量、行走 / 冲刺速度、跳跃弧线、重量和重力、晕眩值、资源槽；
- **招式属性**：伤害、削血、硬直帧、推开、能量获取、补正、属性（上段 / 下段 / 中段、无敌、霸体、投技）、取消规则；
- **规则数据**：连段补正表、反击倍率、低血量减伤、资源的获得和消耗；
- **公式**：初期用表和少量带参数的公式，由 gongfu 自己的定点求值器计算（核心不依赖 openmui.expr）；
- **数据文件**：可读的源文件 `*.fighter.gongfu.toml`、`*.system.gongfu.toml`、`*.stage.gongfu.toml`（gongfu 自己的后缀，和 `.omui` 分开）；
- **构建时烘焙**：判定框、位移、帧数据由 Studio 从动画算出，构建插件 `gongfu.rules` 烘焙成定点数的表，模拟只查表、不采样动画（回滚便宜、结果确定）；
- **检查**：加载时逐项检查，带行号、列号和最接近的正确写法；不合规的角色不能进对战。

### 8.4 画面层（核心之外）

- 模拟每帧给出 `view`：角色、招式、帧序号、朝向、位置、命中事件、特写提示；
- `gongfu.present2d`：`render.sprite` / `render.vector` + hanim 的 2D 骨骼；
- `gongfu.present3d`（以后）：`render.mesh` + hanim 的 3D 骨骼、混合、IK + 特写镜头；
- **画面是（模拟状态, 帧号）的纯函数**：粒子由（事件帧, 实体, 种子）决定，特写是时间线资源；回滚时不需要保存任何画面状态。
- **特写**：必杀技的冻结帧在招式数据里；镜头、动画、特效、音效对齐到提示帧。
- **打击感是每个招式的一份数据（冲击配置）**：双方的打击停顿帧数、震屏曲线、镜头推拉、冲击帧（反色、闪白）、火花粒子、分层音效（起音、主体、尾音，和命中帧精确对齐）、手柄震动、击倒慢动作；在训练模式里逐帧可见、可调。

### 8.5 训练模式和工具

- **训练模式**（openmui 视图）：判定框显示、帧表（类似街霸 6 的 frame meter）、输入显示、录制和回放假人、状态存档和读档、逐帧前进、连段试炼、伤害读数；
- **`gongfu.studio`**：同一条时间线上做 hanim 动画（曲线、洋葱皮）、编辑判定框和帧数据、预览打击感；数据文件是人和 agent 都能读写的格式；它是真实的制作工具，也是 openmui 的作品（D-61：命令行和图形两种界面）。

### 8.6 验收

**自动（进 CI）**：

| 项 | 标准 |
|---|---|
| 回滚 | 一万次随机深度（1–10）、随机时刻的回滚，状态和不回滚的直接模拟逐位相同 |
| 跨平台 | 录制的对局在六个平台（Linux GCC / LLVM、Linux arm、macOS、Windows、wasm）上回放，最终状态哈希相同 |
| 帧数据 | 每个招式在模拟里实测的发生 / 持续 / 收招 / 帧优势和数据声明一致 |
| 输入 | 搓招识别的用例（宽容、冲突、SOCD、同时按键） |
| 性能 | 单帧模拟、8 帧重算的耗时进预算 |
| 数据检查 | 每类错误都有带位置的诊断，CTS 覆盖 |
| 延迟 | 桌面宿主测量输入到上屏，进报告 |

**人工**：试玩的手感和平衡；每个角色让玩家明白它展示的能力。

### 8.7 格斗游戏（`showcase/fighting`，暂名）

简约，但是严肃的格斗游戏；2D 风格化。5 个角色，各有独特的玩法、性格、打击感，并各自展示一项框架能力：

| 角色 | 类型 | 性格 | 展示的能力 | 打击感 |
|---|---|---|---|---|
| **墨**（书法家） | 远程牵制 | 沉静、克制 | 字形级动画：飞行道具是一个正在被写出来的汉字；Slang 的墨迹晕染 | 墨点飞溅、宣纸撕裂、笔锋顿挫的停顿 |
| **霓**（街头） | 贴身猛攻 | 张扬、快 | GPU 效果：霓虹泛光、扫描线、由历史算出的拖影 | 停顿短、段数多、电光、色散 |
| **璃**（玻璃） | 布阵设套 | 优雅、冷 | 液态玻璃的背景折射（D-64）、碎片粒子 | 碎裂声、命中处的折射扭曲 |
| **岳**（巨汉） | 投技 | 沉稳、讲武德 | 镜头冲击和画面形变、分层的低频音效、手柄震动 | 停顿很长、地面裂开、全屏震动、低沉的轰鸣 |
| **刻**（时间） | 场地控制 | 玩世、神秘 | 确定性本身：倒回自己 1 秒前；分身重放她过去 2 秒的输入（会话录制 + 确定性模拟） | 时停的冻结帧、单色画面、钟表滴答 |

可选的第 6 个：**律**（乐手，节奏型）：踩准节拍出招有加成，场景音乐随战况变化（展示音频引擎）。

先把 2 个角色做完整（普通技、必杀技、超必杀、投技、完整帧数据），跑通模拟、画面、打击感、训练模式整条链；再扩到 5 个。美术许可 CC BY-NC（允许改作，不许商用），代码 Apache-2.0。

### 8.8 素材用框架自己的工具做

| 素材 | 工具 | 同时展示 |
|---|---|---|
| 角色造型和部件 | 矢量编辑器（画布 + canvas-kit 的选择、拖动、缩放、吸附） | 画布、行为系统 |
| 动画、帧数据、判定框 | `gongfu.studio` | hanim、时间线、AI 原生编辑 |
| 特效 | 风格工坊扩展出的效果和粒子编辑器（热重载约 50ms） | Slang 效果 |
| 音效 | `media.audio` 上的程序化合成和分层音效工具 | 音频引擎 |
| 场景音乐 | 小型音序器（tracker），以后编曲软件的前身 | 音频图、时间线 |
| 宣传片 | 确定性的对局回放 → openmui-video 导出（S5） | 视频导出 |
| 3D（以后） | Blender 建模，glTF 导入；不做建模软件 | — |

工具解决不了美术水平：每个角色先定美术方向（剪影、配色、动作节奏），由维护者把关。工具放在哪里（`showcase/` 还是 `tools/`）在各自那一轮决定（Q-27）。

### 8.9 Toy 版

- 渲染：WebGPU，没有就 WebGL2；
- 容器：横屏 + 沉浸，可用尺寸和安全区接窗口信号；
- 操作：手机上触屏的简易操作；PC 网页上键盘和手柄，可以本地双人；
- 音频：Web Audio，第一次点击时解锁；
- 平台功能：排行榜（街机、生存、计时、每日固定种子的挑战，只在结算时提交）、云存档（关键节点批量写）、战绩海报（框架渲染）、二维码接力；
- 联机：Toy SDK 没有实时网络，需要自己的信令服务 + WebRTC，放到后面（Q-28）；首发是对电脑、本地双人和训练模式；
- 只构建和检查 Toy 包，不发布。

### 8.10 以后：3D 和创意工坊（只预留，不做）

- **3D**：`present3d` + `render.mesh` + hanim 3D；社区设计的是招式编排、套路和数值，外观靠基础身体、动作库（重定向）、材质和特效定制，完整自制模型只留给高级创作者；
- **创意工坊**：两层——竞技池（官方 + 审核晋升的社区角色，按赛季冻结，排位只用它）和创意池（娱乐和自定义房间）；**数值预算**（合法角色的边界，设计时当场检查）；**AI 自动试打**（确定性 + headless，新角色和全部角色对打几千局，生成平衡报告）；回放可以在任何平台复核；
- **代码 mod**：WASM 沙箱（mcpp-index 已有 `compat.wamr`），状态在线性内存里，回滚整块拷贝；
- Steam：Steamworks 作为可选宿主插件 `host.steam`。

现在要做到的只是：数据格式就是 mod 格式、加载时完整检查、核心和画面分离、状态可以整块拷贝。

---

## 9. 媒体（D-86）

| 包 | 内容 |
|---|---|
| `media.audio` | 设备输入输出（miniaudio：桌面和 Web Audio）；音频图（混音、声部、效果，和帧管线同一种描述方式，在音频线程按块运行，每块零分配）；和模拟 / 动画事件采样精确对齐；**离线渲染逐位确定**（视频导出和测试用，各平台同一个哈希）；Opus、WAV、FLAC |
| `media.video` | 帧管线把“呈现”换成“编码”；帧之间独立，可以并行；和音频按样本同步（第 i 帧的样本是 ⌊i·rate·den/num⌋ 起，全是整数）；编解码是插件：内置 PNG 序列、Y4M（BT.709 4:2:0，整数转换）和 WAV，不带第三方代码；压缩格式是单独的插件包：WebM（libwebm）里的 VP9（libvpx）和 Opus（libopus），都是 BSD、经 mwrap 引入（`openmui.video.webm`）；以后可选 ffmpeg（LGPL、动态链接）、系统编码器、Web 上的 WebCodecs；`*.video.omui.toml` 描述一个视频；命令行是 `omui video` |
| `media.timeline` | 轨道、片段、时间映射、关键帧（hanim 的曲线）的数据模型；剪辑软件、编曲软件、动画编辑、`gongfu.studio` 共用；界面组件是 `components/timeline` |
| 运行时：撤销和事务 | reversible 的动作提供逆操作；事务把一组动作合成一个；agent 的 dry-run（之前计划里的待办） |

**以后的创作软件**（各自是独立的作品，不进 openmui 核心）：

- **AI 原生的剪辑软件**：每个编辑操作是一个动作，带 openas 的效果等级（剪切 reversible、删除素材 irreversible、导出 external）；工程文件是人和 AI 都能读写的数据；任意时刻的画面都能在 headless 下确定地渲染，AI 改完可以“看一眼”；
- **编曲软件**：音频图 + CLAP / VST3 插件宿主 + MIDI + 时间线（钢琴卷帘、编排）+ 自动化曲线（hanim）+ 确定的离线导出。

---

## 10. 实施顺序

每轮一个 commit，七个 CI 作业和每晚的 sanitizer 通过后接着做下一轮。

| 轮 | 内容 | 验收 |
|---|---|---|
| 1 | D-80 ①：搬目录、分组 README、新主 README、lint `layout` / `thirdparty` / `licenses`；AGENTS 和设计文档修订 | 全部测试照常通过；lint 自测能拦下错位的成员、缺 README、GPL 依赖 |
| 2 | D-80 ②：改名（hanim、shader、session、render.effect、ui.canvas.kit） | 同上；像素哈希不变 |
| 3 | D-81 ①：`render` / `render.vector` 拆分；rhi 加深度、MSAA、compute、时间戳 | 像素哈希不变；rhi.cts 新用例 |
| 4 | W1：wasm 构建，CI 的 wasm 行（node + headless） | 哈希和桌面相同；体积写进预算 |
| 5 | W2：`host.web`（语义 DOM 镜像、输入法、页面 agent）+ `rhi.webgpu` | 浏览器里跑示例 01 和 S1 缩小版（M6 ①） |
| 6 | W3：`site/` 和 Pages 部署 | 预渲染的 HTML、可运行的作品、四种渲染的镜头 |
| 7 | `rhi.webgl` + `host.web.toy` + `dist-toy` | WebGL2 的 CTS；toy_doctor 通过 |
| 8 | `media.audio` + mgame 核心（world、sim、input、physics）；S4 打砖块 | S4 的自动验收（实施计划 §19.3） |
| 9 | gongfu 核心 + `present2d` + 训练模式 + `gongfu.studio`；格斗游戏的前 2 个角色 | §8.6 的自动验收 |
| 10 | 格斗游戏扩到 5 个角色；Toy 版 | 每个角色的打击感和能力展示 |
| 11 | D-86：`media.video`、`media.timeline`、撤销和事务；S5 宣传片 | S5 的验收（§19.3） |

之前计划里剩下的事项按依赖插进各轮：S3 agent 操作和 agent 的观察能力（区域外框、dry-run，和第 11 轮的事务一起）；D-71 到 D-73；builder 语法；契约 ⑦ 写进规范；Inspector 的火焰图和动画检查器；D-70 的平台映射（需要 ProMotion / VRR 硬件）。

---

## 11. 决策

| 编号 | 决策 |
|---|---|
| D-80 | 目录与命名：路径就是包名（分组目录只做归类）；`modules/{base,gfx,content,media,ui}`，宿主在 `modules/ui/host`；按角色命名；动画引擎叫 `hanim`；`slang` → `shader`、`record` → `session`、`effect` → `render.effect`；每个分组目录有 README 并由 lint 检查；主 README 以作品为中心，英文为主、附中文 |
| D-81 | 统一渲染：rhi → shader → render（渲染图）→ 渲染特性；2D 和 3D 共用一套架构；rhi 的范围由渲染特性的需要决定；`render.vector` 保留 CPU 参考和逐字节金图 |
| D-82 | 产品分层：基础组件、openmui、mgame、gongfu、作品；依赖只向下；openmui 是独立的 UI 框架，mgame 把它当 UI 模块；用多少带多少，hello 应用的体积和依赖闭包由 CI 检查；gongfu 核心只依赖标准库，接口粗粒度，以后可以拆出 |
| D-83 | 第三方库：`compat.<库>` + `mcpplibs.mwrap.<库>`，只在 `native/` 分区使用；上游先做；没有 GPL / AGPL（动态链接也不行）；LGPL 只能可选、动态链接；MPL 不修改即可用；默认免版税格式；lint `licenses` 检查依赖图 |
| D-84 | Web 与 Toy：wasm 单线程；`host.web` 带语义 DOM 镜像和页面 agent；WebGPU 为主、WebGL2 兼容、CPU 兜底；wasm 的像素哈希和桌面相同；`host.web.toy` 把 Toy 的能力映射成 openas 的动作（限流是退出码 3）；`dist-toy` 只打包和检查，不发布；官网是创意设计，构建时预渲染静态 HTML，部署到 GitHub Pages；小红书不支持 |
| D-85 | mgame 与 gongfu：mgame 是引擎服务（world、sim、input、physics、net、assets、ui）；gongfu 是格斗框架：定点数 60Hz 确定性模拟、回滚、数值系统、数据驱动的角色（格式即以后的 mod 格式）、构建时烘焙帧数据、模拟和画面分离（画面是纯函数），2D 先验证、3D 同一核心；格斗游戏 5 个角色各展示一项框架能力；创意工坊以后做 |
| D-86 | 媒体：`media.audio`（音频图、离线渲染逐位确定）、`media.video`（呈现换成编码，编解码插件）、`media.timeline`（共用的时间线模型）、撤销和事务；剪辑软件和编曲软件是以后的独立作品 |

修订：D-55（独立性只约束 `modules/{base,gfx,content,media}`）、D-58（`modules/gfx/rhi`；rhi 不再限于 UI 规模）、D-76（引擎服务就是 mgame，openmui 是它的 UI 模块）、实施计划 §20（rhi 范围的风险）。

## 12. 待决问题

| 编号 | 问题 | 什么时候定 |
|---|---|---|
| Q-20 | 对外品牌：mgame、gongfu、格斗游戏的正式名字；基础组件是否换中性命名空间（现在用 `openmui.`）；hanim 发布到 mcpp-index 时的包名 | 公开发布前 |
| Q-21 | WebGL2 的着色器：Slang 直接输出 GLSL ES，还是 SPIR-V 经 SPIRV-Cross 转换 | 第 7 轮 |
| Q-22 | B 站 App 内 WebView（iOS / Android）的 WebGPU 覆盖率 | 第 5 轮实测 |
| Q-23 | wasm 的体积预算（hello、文档墙、格斗游戏） | 第 4 轮测出后 |
| Q-24 | gongfu 定点数的精度和单位（例如 1/256 像素）、公式的表达能力 | 第 9 轮之前 |
| Q-25 | `mgame.world`：EnTT、flecs，还是自写列存储（回滚要求整块拷贝） | 第 8 轮 |
| Q-26 | hanim 的 3D 骨骼：引入 ozz-animation，还是参考它的设计自写 | 3D 之前 |
| Q-27 | 矢量编辑器、音效工具、音序器放在 `showcase/` 还是 `tools/` | 各自那一轮 |
| Q-28 | 联机：信令服务和传输（Toy 没有实时网络） | 第 10 轮以后 |
| Q-29 | 官网是否暴露 WebMCP（浏览器支持时） | 第 6 轮 |

---

## 13. 实施拆分与任务依赖

### 13.1 依赖图

```
R1 目录与命名（D-80，一轮完成搬目录 + 改名 + README + lint）
 ├─▶ R2 渲染核心拆分 + rhi 扩展（D-81 ①）
 │    ├─▶ W2 host.web + rhi.webgpu ─▶ W3 官网 + Pages
 │    │                            └─▶ R7 rhi.webgl + host.web.toy + dist-toy
 │    └─▶ R9 render.sprite（gongfu 的 2D 画面需要）
 ├─▶ W1 wasm 构建 + CI 的 wasm 行 ─▶ W2
 └─▶ R8 media.audio + mgame 核心 + S4 ─▶ R9 gongfu + 前 2 个角色 ─▶ R10 5 个角色 + Toy 版（还需要 R7）
                                    └─▶ R11 media.video + timeline + 撤销和事务 + S5（还需要 R2）

可以和主线并行的上游工作（在 mcpplibs / mcpp-index 的独立 worktree 里，由子代理完成，最多 3 个）：
  U1 wasm32-emscripten 上的依赖：mcpplibs.os、compat.harfbuzz / fribidi / libpng / md4c 等在 emscripten 上构建和测试 → W1
  U2 mwrap：miniaudio（已有 compat）、Box2D v3、libopus、dr_libs → R8
  U3 mwrap：basis_universal、SPIRV-Cross（视 Q-21）→ R7 / R9
```

### 13.2 每轮的内容和本地验证

| 轮 | 内容 | 本地验证（提交前） |
|---|---|---|
| R1 | 按 §3.2 搬目录（`git mv`）；§3.3 的改名（`hanim`、`openmui.shader`、`openmui.session`、`openmui.render.effect`、`openmui.ui.canvas.kit`；`lang/common` 的内容移到 `lang/` 作为 `openmui.lang` 的包目录）；重算所有 `path` 依赖；CI 脚本、lint 层表、生成器和测试里的路径；分组 README；新主 README；lint `layout` / `thirdparty` / `licenses` 和它们的自测；AGENTS.md、实施计划、架构文档同步 | 清掉旧的构建目录后完整构建；`mcpp test --workspace`；lint 和自测；模板检查；像素哈希不变 |
| R2 | `render` / `render.vector` 拆分；rhi 的深度 / 模板、MSAA、compute、时间戳；rhi.cts 新用例 | 全部测试；GPU 测试在 lavapipe 和本机 GPU 上 |
| W1 | wasm 构建；单线程执行器；CI 的 wasm 行（node 跑 headless） | 本地 emsdk 构建，node 跑测试，哈希和桌面相同 |
| W2 | `host.web`（画布、输入、输入法、剪贴板、语义 DOM 镜像、页面 agent、rAF 与 FrameDemand）+ `rhi.webgpu` | 本机浏览器；**Pages 上的测试页**（见 13.3） |
| W3 | `site/`：预渲染、可运行的作品、四种渲染的镜头 | 本机浏览器和 Pages |
| R7 | `rhi.webgl`、`host.web.toy`（模拟的 `window.toy` 用于测试）、`dist-toy` + `toy_doctor.py` | 浏览器；toy_doctor 通过 |
| R8 | `media.audio`、`mgame.{world,sim,input,physics,ui}`、S4 打砖块 | S4 的自动验收 |
| R9 | `gongfu` 核心、`gongfu.rules`、`present2d`、训练模式、`gongfu.studio`；2 个角色 | §8.6 的自动验收 |
| R10 | 5 个角色；Toy 版 | 同上；Toy 包检查 |
| R11 | `media.video`、`media.timeline`、撤销和事务；S5 | S5 的自动验收 |

每轮一个 commit 推到 PR #1；等 CI 的时候做下一轮里不依赖这次结果的部分（例如上游的 U1–U3、下一轮的设计细节和测试）。主观的设计问题（官网创意、角色设计、README）在定稿前请 codex 给一次评价，作为参考之一，和本文的规则、已有的设计一起权衡。

### 13.2a 进度和剩下的拆分（2026-10-30）

已完成：R1、R2、W1、`host.web` 的第一部分（画布、帧节奏、输入、手柄、CPU 渲染）、`host.web.toy` 和 `dist-toy`、R8（`media.audio`、mgame 核心、S4）、R9 和 R10（gongfu、训练模式、工具、六个角色全部换成生成的画，三气、胜利姿势、语音、主题曲和场景、三局两胜、Toy 版）；T1（撤销、事务和 dry-run，规范 S-12、S-13）；T3（`media.video`：按样本精确的帧和声轨、内置 PNG 序列 / Y4M / WAV、`*.video.omui.toml`、`omui video`；WebM 插件 `openmui.video.webm`：VP9 + Opus，上游 compat.libvpx、compat.libwebm、mcpplibs.mwrap.libvpx 已发布进 mcpp-index）。

T5 的第一部分已完成（2026-10-30）：`site/` 的地方、四档镜头、树哈希对照、中英文、可用的时间线编辑器、构建时预渲染、Pages 工作流；其余部分列为 T5b。T5b 已完成 agent 面板（S-10、S-11 的演示）、链接（`Runtime::open_url`）、镜头的淡入和切换语言的字形过渡（字形动画作用于子树）。

T6 的前半已完成（2026-10-30）：`rhi.webgpu`，rhi.cts 在浏览器的 WebGPU 上全部通过（§6 的现状）；后半是 `rhi.webgl`。

剩下的按依赖拆成七项，T1–T4 互不依赖，可以并行（子代理最多 3 个）：

| 项 | 内容 | 依赖 | 本地验证 |
|---|---|---|---|
| T1 | 运行时的撤销和事务：reversible 的动作带逆操作，事务把一组动作合成一个（一起成功或一起撤回），agent 的 dry-run（只算结果不提交） | — | core / view 的测试；agent 协议的测试 |
| T2 | `media.timeline`：轨道、片段、时间映射、关键帧（hanim 的曲线）的数据模型，读写 `*.timeline.omui.toml`；之后 `components/timeline` | — | 读写回自身、求值确定、wasm 上同样的结果 |
| T3 | `media.video`：帧管线把呈现换成编码；内置 PNG 序列和 Y4M、WAV 音轨；WebM（libwebm）+ VP9（libvpx）经 mwrap 作为插件（上游先做）；和音频按样本同步 | — | 离线导出逐位确定（哈希固定）；编码后解码回来对照 |
| T4 | W2 剩下的：输入法的隐藏 textarea、剪贴板、语义 DOM 镜像、`window.omui`（经 `openas::decide`）、`Store`（localStorage） | — | Web 行（node）里用模拟的 DOM 测；本机浏览器 |
| T5 | W3：`site/`、构建时预渲染、Pages 部署 | T4 | 本机浏览器；Pages |
| T5b | §6.6 的其余部分：无限画布上的“地方”（全景、缩放进入）；镜头的连续过渡；页面底部的时间轴（拖动、倒放，这一帧的哈希和 CI 对照）；切换语言时的字形过渡；可运行的作品（文档墙、风格工坊、示例 01、格斗出招表）；agent 面板（S-11：未授权被拒，授权后运行） | T5 | 本机浏览器；Pages |
| T6 | `rhi.webgpu`（Slang → WGSL），然后 `rhi.webgl` | R2 | rhi.cts；浏览器 |
| T7 | S5 宣传片 | T1、T2、T3 | S5 的验收 |

### 13.3 GitHub Pages

仓库的 Pages 已经开启（`build_type: workflow`，地址 `https://sunrisepeak.github.io/mui/`）。`github-pages` 环境现在只允许 `main` 部署；W2 起在环境的部署分支规则里加上 `openmui-design`，PR 分支上的 Pages 工作流就能部署测试页：W2 的浏览器测试页（示例 01、S1 缩小版、帧哈希对照）、W3 的官网、R7 的 WebGL2 和模拟 Toy 环境。
