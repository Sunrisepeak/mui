# openmui 实施计划

> 状态：**草案 v1.3**（2026-10-26），待 review
> 依据：[architecture](2026-10-26-openmui-architecture-design.md)（D-01 到 D-37）、[spec](2026-10-26-openmui-spec-design.md)、[component-binding](2026-10-26-openmui-component-binding-design.md)、[view-data-files](2026-10-26-openmui-view-data-files-design.md)、[canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md)。
> 本计划新增的决策是 **D-38 到 D-57**（§21），已经同步到架构文档 §13。**验收标准见 §19。**
> 写法说明：标注了路径或来源的内容是事实；性能数字都是**目标值**，需要实测校准；命令和 API 是示意。

---

## 0. 一页摘要

| 方面 | 计划 |
|---|---|
| 语言 | **C++23 是基线**（全部模块化，`import std`）；用 GCC 16 时可以选用 C++26（反射、`template for`、`std::inplace_vector`），但只限定在**单独的成员包**里（§1.1） |
| 规则 | **不用宏，用 constexpr**；**头文件只有 `openmui.h`（C ABI）**；核心不用异常和 RTTI；热路径没有虚函数，每帧零分配 |
| 工程 | 一个仓库，多个 **workspace 成员**。每个成员有独立的 `mcpp.toml`，就是将来可以拆分出去的独立仓库（§2） |
| 平台 | OS 服务通过 git 依赖 **`mcpplibs/os`（分支 `os-0.1.0`）**；窗口和输入走 SDL3；平台差异用“清单按目标选择模块分区 + constexpr 常量”来处理（§8） |
| 动画 | **通用动画引擎 `hanim`**：独立的 workspace 成员，只依赖标准库，将来可以独立成仓库；**时间的纯函数**（可以任意跳转、结果确定，spring 有解析解）；UI、视频、游戏、画布共用；**提前到 M2**（§6） |
| 展示 | S1 无限画布 Markdown 墙 · S2 风格化 GPU 渲染 · S3 Agent 操作 · **S4《组件打砖块：openmui 巡礼》**（游戏本身就是框架导览）· **S5 openmui 宣传片**（约 60 秒，用 openmui-video 制作，对标播放量几百万到上千万的 agent 代码动画视频，拆解和分镜见 [promo-video](2026-10-26-openmui-promo-video-design.md)）（§13） |
| 视频 | **性能和编辑体验全面领先业界**：60 秒 1080p60 完整渲染 ≤ 15 秒（GPU），修改一个镜头后整片重新渲染 ≤ 3 秒（四层缓存），预览热重载 ≤ 100 毫秒，修改可以从预览写回（§12.6） |
| 测试 | 单元测试、CTS 的 S / A / B / C 四级、CPU 金图逐字节比对、GPU 金图（lavapipe）、跨平台确定性、三种语言的语义一致性、性能门禁、模糊测试和 sanitizer（§15） |
| 生态 | 需要补充到 mcpp-index、xim、mcpplibs、mcpp 的内容逐项列出（§17） |
| 里程碑 | M0 骨架 → M1 核心 → M2 文本 + 动画 + Markdown → M3 声明式 + 三种语言 → M4 GPU + 画布 + 风格化 → M5 视频 + 游戏 + Agent → M6 打磨和跨平台（§18） |
| 验收 | 全局质量门禁 + 每个里程碑、模块、展示各自的验收标准，**能自动检查的都进 CI**（§19） |
| 协作 | **单个 PR，每一轮修改提交一个 commit 作为检查点**，方便审查（§2.5） |

---

## 1. 技术基线与工程规则

### 1.1 语言标准与工具链

| 项目 | 规定 |
|---|---|
| 基线 | `[language] standard = "c++23"`，`modules = true`，`import std`。**核心的所有成员包都必须能以 C++23 编译** |
| 主工具链 | GCC 16（mcpp 的 `gcc@16.1.0`）。其他平台使用 mcpp 在该平台上的默认工具链 |
| C++26 | GCC 16.1 已经提供实验性的 P2996 反射（需要 `-std=c++26 -freflection`），以及 `template for`（`-fexpansion-statements`）、`std::inplace_vector` 等（来源见文末）。**只在单独的成员包 `openmui-reflect` 里使用**：应用依赖它，就选用了反射；不依赖它，整个依赖图仍然是 C++23 |
| 反射的用途 | 只用于**提升易用性**，不是必需的：结构体自动生成表单、模型列、视图文档和语义节点；C++ 实现和契约的一致性检查。**每一处都有 C++23 下的替代方案**（由契约生成代码） |
| 编译器开关 | 核心不用异常和 RTTI；动画、布局、CPU 光栅器这几个模块通过 mcpp 的按文件 flags 设置 `-ffp-contract=off`，并且禁用 fast-math，**保证浮点结果跨平台一致**（A / B 级确定性） |

### 1.2 不用宏，用 constexpr（D-38）

| 场景 | 做法 |
|---|---|
| 平台、架构、特性的判断 | **清单按目标选择模块分区**，分区里提供 `inline constexpr` 常量，代码里用 `if constexpr` 分发。这和 `mcpplibs/os` 的规则 R3 / R5 一样：平台专属的函数在所有目标上都声明，只在自己的平台上定义；在别的平台上误用会**链接失败**，而不是运行时出错 |
| 编译期校验 | `consteval`：ID 字面量的格式、格式化字符串、契约里的名字 |
| 断言和日志 | 普通函数加 `std::source_location`，不用 `assert` 宏和日志宏 |
| C 头文件里的宏 | 只在 `native/` 绑定模块里读取 C 头文件，把宏转换成 `constexpr` 常量或内联函数再导出（照搬 imkit、xdesk 的做法） |
| 生成的代码 | 生成器产出的 C++ 同样不含宏 |
| **唯一的例外** | `openmui.h`：C 头文件需要 `extern "C"` 的判断和导出可见性。它由生成器产出，宏的数量保持在最少，而且版本号同时提供一个 enum 常量形式 |

示意（平台常量，没有宏）：

```cpp
// runtime/platform/src/platform.cppm
export module openmui.platform;
export import :target;                  // 由清单按目标选择：src/targets/{linux,macos,windows,web}.cppm

// runtime/platform/src/targets/linux.cppm
export module openmui.platform:target;
export namespace omui::platform {
    enum class os { linux, macos, windows, web, android, ios };
    inline constexpr os current = os::linux;
    inline constexpr bool desktop = true;
}
```

```toml
# runtime/platform/mcpp.toml
[target.linux.build]
sources = ["src/targets/linux.cppm", "src/linux/*.cpp"]
[target.windows.build]
sources = ["src/targets/windows.cppm", "src/windows/*.cpp"]
```

### 1.3 头文件收敛到最少（D-40）

- **整个项目只有一个头文件：`spec/include/openmui.h`**（C ABI，由 registry 生成）。
- 其他全部是模块：接口单元 `.cppm`，实现单元 `.cpp`。
- 第三方的 C / C++ 头文件只出现在各成员包的 `native/` **实现分区**里，并且不导出；用 lint 检查（参考 xlings 的 `lint_c_headers.sh`）。
- 模块接口里只出现标准库和 openmui 自己的类型，**不出现原生句柄**（参考 `mcpplibs/os` 的规则 R4）。

### 1.4 性能规则

| 规则 | 做法 |
|---|---|
| 数据导向 | 节点存储用 SoA 加带代数的句柄（slot map）；脏标记用位集合；遍历时缓存友好 |
| 每帧零分配 | 每帧一个 arena，帧结束整体重置；对象池；`std::inplace_vector`（C++26，只在 GCC 下选用）或自己实现的定长向量 |
| 热路径没有虚函数 | S 边界用 concepts / CRTP；L 边界只在粗粒度的地方做类型擦除（例如一个渲染后端一个虚表） |
| 不用异常和 RTTI | 错误用 `std::expected` |
| 并行 | 任务系统：工作窃取线程池、优先级、取消（`std::jthread` + `std::stop_token`）；布局测量、文本排版、光栅化、视频帧渲染都可以并行 |
| 先测量 | 每个模块都带基准测试；CI 里有性能门禁（§15） |

### 1.5 API 的优雅和简洁

- **核心概念只有五个**：Node（节点）、Signal（状态）、Verb（命令）、View（视图）、Effect（效果）。其他都是由它们组合出来的。
- **选项用指定初始化器**：`omui::button({.id = "inc", .text = "加一"})`。没有长长的参数列表，也没有 builder 链。
- **值语义**，没有输出参数；`[[nodiscard]]`；强类型 ID（`NodeId`、`VerbId`，不是裸整数）。
- **每个模块都很小**，用分区来组织；对外只有一个主模块。
- **注释写“是什么、为什么”**（和 Luban 的代码风格一致），不写“怎么做”。

---

## 2. 工程划分：一个仓库，多个可以独立的 workspace 成员（D-41）

### 2.1 原则

1. **每个成员都是一个完整的 mcpp 包**：有自己的 `mcpp.toml`、版本号、README、`tests/`、许可证声明。
2. **成员之间只通过 `path` 依赖互相引用**。拆分出去的时候，只需要把 `path` 改成 `version` 或 `git` 依赖（和 Luban 的规范“移出仓库”的成本一样低）。
3. **不允许跨成员用相对路径引用源文件**；依赖方向用 lint 检查。
4. **每个成员可以单独构建和测试**：`mcpp build -p <member>`、`mcpp test -p <member>`（具体参数以 mcpp 为准）。
5. tag 按成员打：`spec-v0.1.0`、`runtime-core-v0.1.0` …（每个包单独打 tag、单独发布，需要在 M0 验证，见 V-02）。

### 2.2 目录树

> **2026-10-28 修订（D-80）**：目录按层重新分组，现行的目录树见 [structure-render-web-game](2026-10-28-openmui-structure-render-web-game-design.md) §3.2（`modules/{base,gfx,content,media,ui}`、宿主在 `modules/ui/host`、hanim、shader、session）。下面是最初的规划，保留作为记录。

```
mui/                                   （仓库根目录 = openmui 伞包 + workspace；内部一律叫 openmui）
├── mcpp.toml                          # [package] openmui · [workspace] members · [features] rules / entry-player / entry-app
├── src/
│   ├── openmui.cppm                   # 伞模块：export import openmui.core / view / ui.base / theme.default …
│   └── entry/{player.cpp,app.cpp}     # 入口（运行时代码；mcpp#845 落地后由特性提供）
├── mcpp/
│   └── rules/                         # 构建插件 openmui.rules（只放构建系统相关的代码）
├── templates/                         # mcpp new 的模板（mcpp 约定必须放在包的根目录）
│
├── spec/                              # ★ openmui.spec：规范（只依赖标准库）
│   ├── SPEC.md
│   ├── registry/                      #   roles / actions / effects / motion / expr / effect / extensions / profiles / components / schemas
│   ├── include/openmui.h              #   全仓库唯一的头文件（生成）
│   ├── src/                           #   值类型模块 openmui.spec（生成 + 少量手写）
│   └── conformance/{S,A,B,C}/         #   CTS
│
├── modules/                           # ★ 独立组件（D-55、D-57）：不依赖 openmui 的任何其他包，随时可以拆成独立仓库
│   ├── geom/                          # ★ openmui.geom：点、尺寸、矩形、边距、颜色、仿射变换、像素对齐（modules 共用的几何类型）
│   ├── anim/                          # ★ hanim：**通用动画引擎**（只依赖标准库；UI、视频、游戏、画布共用，§6.0）
│   ├── layout/                        # ★ openmui.layout：flex / grid / 栈 / 绝对定位 / 滚动的布局引擎（自带节点存储，增量、带缓存、结果确定）
│   ├── unicode/                       # ★ openmui.unicode：UAX#14 断行、UAX#29 字素，表由 tools/gen 从 Unicode 数据生成
│   ├── text/                          # ★ openmui.text：字体、排版、断行、字体回退、编辑（依赖 unicode、HarfBuzz）
│   ├── raster/                        # ★ openmui.raster：确定性的 CPU 2D 光栅器（路径、描边、渐变、合成）
│   ├── image/                         # ★ openmui.image：图像缓冲、PNG / JPEG 编解码（libpng、zlib）
│   ├── render/                        # ★ openmui.render：**渲染层抽象**：显示列表、渲染器接口、CPU 后端（基于 raster）、GPU 后端（基于 gpu）、内置 Slang 着色器
│   ├── rhi/                           # ★ openmui-rhi：UI 尺度的 GPU 抽象层接口 omui.rhi
│   ├── rhi-sdl/                       # ★ openmui-rhi-sdl：后端（Vulkan / Metal / D3D12）
│   ├── rhi-gles/                      #   openmui-rhi-gles（M6：嵌入式 Linux、WebGL2）
│   ├── rhi-webgpu/  rhi-vk/           #   以后再做
│   ├── pipeline/                      # ★ openmui.pipeline：阶段插件组成的管线：带类型的资源、校验、确定的顺序、按阶段计时和追踪、运行中替换（D-66）
│   ├── trace/                         # ★ openmui.trace：span / 计数器 / 标记，按线程无锁记录，导出 Chrome trace，按 span 汇总百分位（D-59）
│   ├── record/                        # ★ openmui.session：录制和回放：带时间戳的事件 / 动作流（NDJSON），可以跳转、可以转成测试
│   └── encode/                        # ★ openmui-encode：文件输出：PNG 序列、视频编码（经 mcpplibs.os 启动 ffmpeg 并用管道送帧，可选硬件编码）、音频混合
│
├── runtime/
│   ├── core/                          # ★ openmui.core：signal、verb、输入事件和按键、arena、句柄、JSON、任务调度、诊断
│   ├── view/                          # ★ openmui.view：节点树、组件模型、帧流水线、视图 IR、对账、立即模式（openmui.im）、保留模式（openmui.rt）、表达式（openmui.expr）、视图文档
│   ├── semantic/                      # ★ openmui.semantic：语义树、动作执行、效果分级、信封、查询
│   ├── agent/                         # ★ openmui.agent：openmui.tree@0.1 本地端点（mcpplibs.os.ipc）、协议
│   ├── platform/                      # ★ openmui-platform：平台服务（**全部经由 mcpplibs/os**，每个服务标注效果分级）
│   ├── abi/                           # ★ openmui-abi：openmui.h 的实现（C ABI 的对外导出）
│   ├── player/                        # ★ openmui-player：加载清单、视图文档和脚本
│   ├── reflect/                       # ★ openmui-reflect：C++26 反射的易用层（只在 GCC 16 下可选）
│   └── devtools/                      # ★ openmui-devtools：检查协议、遥测、基准测试接口、像素溯源
│
├── hosts/
│   ├── sdl3/                          # ★ openmui.host.sdl3：桌面（以后包括移动）
│   ├── headless/                      # ★ openmui.host.headless：虚拟 surface、注入时钟、输出 PNG（CI 和视频渲染都用它）
│   └── opensal/  web/                 #   以后再做
│
├── components/
│   ├── base/                          # ★ openmui.ui.base：Core 组件
│   ├── markdown/                      # ★ openmui.ui.markdown
│   ├── canvas/  canvas-kit/           # ★ openmui-ui-canvas / -canvas-kit
│   ├── timeline/                      # ★ openmui-ui-timeline：时间轴、播放器控件（视频预览和动画检查器都用）
│   └── data/  editor/                 #   以后再做
├── themes/{tokens,default,compact,styles}/   # styles：风格包（token + Slang 效果 + 动效），S2 使用
├── video/                             # ★ openmui-video：合成、帧、离线渲染、编码（§12；以后可以独立成仓库）
├── lang/{ts,py,lua}/                  # ★ openmui-lang-*：语言绑定
├── tools/
│   ├── gen/                           # registry / 契约 → C ABI、C++ 分区、.d.ts、.pyi、Lua 注解、CTS 骨架、文档表格
│   ├── omui/                          # 命令行：check / fmt / add / agent / video / bench
│   ├── mcp/                           # MCP 服务器：任何 openmui 应用都可以被 agent 操作
│   └── inspector/                     # 检查器（用 openmui 自己写的应用）
├── examples/01-…/ … 13-…/             # 小示例（文档的骨架）
├── showcase/                          # 旗舰展示（§13）
│   ├── markdown-wall/  style-gallery/  agent-ops/  widget-breakout/  promo-video/
├── tests/{support,cts,e2e,bench}/     # support：测试共用的检查（openmui.testing，只作为 dev 依赖）；其余是跨成员的测试：CTS 运行器、e2e、确定性、性能
├── skills/{openmui,openmui-video}/    # 给 agent 用的技能包（SKILL.md + 规则文件），参考 Remotion 的做法
├── docs/{en,zh}/  llms.txt
├── .github/workflows/
├── .xlings.json                       # 工具清单：mcpp、slang、mesa-lavapipe、esbuild、node、uv、python、emsdk、ffmpeg …
├── AGENTS.md
└── .agents/docs/
```

标了 ★ 的是 M0 到 M5 期间会实际建立的成员。

### 2.3 成员一览：以后哪些可以独立成仓库

| 成员 | 包名 | 将来可以独立吗 | 说明 |
|---|---|---|---|
| spec | `openmui.spec` | ✅ 出现第二个实现者时 | 参考 openkal、Vulkan-Docs |
| **modules/base/hanim** | `hanim` | ✅ **通用动画引擎** | 只依赖标准库，不知道 UI 的存在；游戏引擎、其他 UI 框架都可以使用 |
| modules/base/unicode | `openmui.unicode` | ✅ 通用库 | mcpp-index 里没有断行库，所以自己做；也可以贡献给 mcpplibs |
| modules/content/text | `openmui.text` | ✅ 通用库 | C++ 模块化的文本引擎在 C++ 生态里很少见 |
| modules/gfx/raster | `openmui.raster` | ✅ 通用库 | 确定性的 2D 光栅器 |
| modules/gfx/rhi + 各后端 | `openmui-rhi*` | ✅ | UI 尺度的 GPU 抽象层 |
| components/markdown、canvas | `openmui-ui-*` | ✅ | 有独立用户时 |
| video | `openmui-video` | ✅ | 产品形态独立 |
| lang/* | `openmui-lang-*` | ✅ 有独立维护者时 | 参考 OpenTelemetry |
| tools/mcp | `openmui-mcp` | ✅ | — |
| runtime 的其他成员、伞包 | — | 一般留在主仓库 | — |

### 2.4 根目录的 `mcpp.toml`（示意）

```toml
[package]
name    = "openmui"
version = "0.1.0"
license = "Apache-2.0"

[language]
standard = "c++23"
modules  = true

[workspace]
members = [
  "spec", "modules/*",
  "modules/ui/*", "modules/ui/host/*", "components/*", "modules/ui/theme/*",
  "video", "lang/*", "tools/*", "examples/*", "showcase/*", "tests/*",
]

[dependencies]
openmui.core     = { path = "modules/ui/core" }
openmui.view     = { path = "modules/ui/view" }
openmui.ui.base  = { path = "components/base" }
openmui-theme-default = { path = "modules/ui/theme/default" }

[features.rules]
rule_module = "openmui.rules"
sources     = ["mcpp/rules/*.cppm"]

[features.entry-player]
sources = ["src/entry/player.cpp"]      # mcpp#845 落地后改成入口声明
```

### 2.5 协作流程：单个 PR，commit 作为检查点（D-52）

- 当前阶段的工作都放在**同一个 PR** 里（分支 `openmui-design`，PR 合入 `main`），方便集中审查。
- **每一轮修改提交一个 commit 作为检查点**。commit 信息写清楚这一轮改了什么、为什么改，审查时可以按 commit 逐轮查看差异。
- 每个检查点都要满足：文档之间互相一致（决策编号、目录树、里程碑、后缀）；以后有了代码，还要求 CI 全绿。
- 进入实现阶段以后，仍然按里程碑一个 PR；里程碑之内用 commit 作为检查点。

---

## 3. 架构落地

### 3.1 一帧的流水线和线程

```
UI 线程（C++）：
  输入（按帧合并）→ 分发事件 → 执行动作 / verb
  → signal 传播（批量、拓扑排序）→ 对账（立即模式的 build、保留模式的局部重建）
  → 增量布局 → 动画求值（批量、SoA）→ 语义变更 → 显示列表（增量）→ 交给渲染线程
渲染线程：显示列表 → render（批处理、实例化、图层缓存）→ omui.rhi 或 raster → 提交 → 等待帧已显示（帧节奏）
任务线程池：文本排版、图片解码、缩略图光栅化、视频帧渲染、布局测量（大子树）
脚本线程（每种语言一个）：事件处理、verb、业务逻辑 ↔ 通过消息队列和 UI 线程交换值
```

### 3.2 关键数据结构

| 结构 | 设计 |
|---|---|
| `NodeStore` | SoA：类型、父节点、第一个子节点、下一个兄弟、属性块的偏移、脏位；节点用带代数的句柄（`NodeId`）引用 |
| `SignalGraph` | 依赖图是增量维护的；“先推送标记、再按需拉取”；事务内的修改批量提交 |
| `DisplayList` | 每帧 arena 里的 POD 指令流；按节点子树缓存片段；脏区 |
| `AnimSet` | 活跃动画的 SoA（目标属性、曲线类型、参数、起始时间），每帧批量求值 |
| `SemanticDelta` | revision 加有序的 added / removed / updated；给 agent 和无障碍层使用 |

### 3.3 依赖方向（用 lint 强制）

```
modules/*（geom、anim、layout、unicode、text、raster、image、render、rhi、record、encode、trace、pipeline）：只依赖标准库、彼此和第三方库，不依赖任何 openmui 包
anim ← core（时钟来自 anim）     nlohmann.json、taskflow（tf）、concurrentqueue ← core（来自 mcpp-index，D-56）
spec ← core ← view（节点树、组件；把 layout、text、anim 适配到节点上；产出 render 的显示列表）（把 anim 适配到节点属性）← semantic ← agent（另外依赖 platform 的 IPC）
                                         ↑
                    render ← {gpu ← gpu-*, raster, image}
                    host-* ← (core, render)      platform ← mcpplibs/os
component ← (view 的公开 API)      lang-* ← openmui.h ← abi
video ← (view, anim, host-headless, render, ffmpeg)      game / canvas ← anim
```

---

## 4. 规范工作流

### 4.1 每个版本交付什么

| 规范版本 | 和哪个里程碑对应 | 内容 |
|---|---|---|
| 0.1 | M1–M2 | 语义树（S-1 到 S-9）、动作和效果、视图 IR、显示列表、**动画语义和精确公式**（视频依赖它，所以提前）、C ABI 骨架、CTS 的 S 级和 A 级 |
| 0.2 | M3 | 视图文档的 schema、表达式 AST 和 CEL 子集、Script Binding Profile、契约 schema、清单 schema |
| 0.3 | M4–M5 | 扩展：canvas、instanced_draw、shader_effect（效果接口）、render_to_texture、gpu_node；视频合成的语义 |

### 4.2 Registry 和生成器（`tools/gen`）

| 输入 | 输出 |
|---|---|
| `registry/*.toml`、`components/*.contract.omui.toml` | `openmui.h`、值类型模块、契约的 C++ 分区（CRTP 基类、属性存储、动作分发） |
| 同上 | `.d.ts`、`.pyi`、LuaLS 注解、各语言的类型化封装 |
| 同上 | 视图文档的 JSON Schema、组件目录（给 agent 用） |
| 同上 | CTS 骨架、文档里的表格、`llms.txt` 的片段 |

生成器本身是一个 C++ 模块化程序（依赖 mcpp-index 里的 `marzer.tomlplusplus` 和 `nlohmann.json`，两者都以 C++23 模块提供），由 `openmui.rules` 在构建期调用。

### 4.3 流程

规范的每一次修改都必须同时提交：registry 的修改、CTS 用例、参考实现的实现，缺一项就不能合并。较大的改动先写 RFC，放在 `.agents/docs/`。

---

## 5. 运行时各模块的实施

| 成员 | 范围 | 关键点 | 里程碑 |
|---|---|---|---|
| core | 树、ID（S-8 / S-9）、signal、verb、事件、焦点和导航、arena | SoA + 带代数的句柄；`consteval` 校验 ID 字面量；signal 批量提交；**时钟使用 `hanim::Clock`，可以注入**（交互、虚拟、渲染三种） | M1 |
| 任务调度（core 内） | 基于 mcpp-index 的 **taskflow**（模块 `tf`）：帧内的并行 for、任务图、优先级（帧关键 / 后台）、取消；UI 线程和脚本线程之间的队列用 `compat.concurrentqueue` | 不自己写线程池（D-56）；热路径上不分配内存 | M1 |
| layout | flex、grid、栈、滚动、虚拟化 | 增量布局、测量结果缓存、**取整规则**（A 级）、`-ffp-contract=off` | M1–M2 |
| text | HarfBuzz 排版、FreeType 字形、fribidi、**UAX#14 断行**、字体回退、选区和编辑、输入法 | 断行表由 `tools/gen` 从 Unicode 数据生成（不额外依赖 ICU）；排版结果缓存；msdf 图集交给 render | M1–M2 |
| anim | 独立组件 `modules/base/hanim/`，见 §6 | 只依赖标准库 | M2 |
| view | 视图 IR、对账、`openmui.im`、`openmui.rt`、`openmui.expr`、视图文档的加载 | 立即模式在没有事件时不调用 build；表达式 AST 由原生代码求值；CEL 子集解析器要做模糊测试 | M1（im）、M3（rt、expr、视图文档） |
| semantic | 产出语义树、执行动作、效果分级、`needs-answer`、Origin、信封、**录制和回放** | 增量变更；虚拟化语义；录制内容是“语义动作 + 时钟” | M1 |
| agent | `openmui.tree@0.1` 本地端点（`mcpplibs.os.ipc`），协议是 JSON lines | snapshot / changes / find / act / wait / capture；actor 的文本形式和 openas 一致 | M1（基础）、M5（完整） |
| platform | 文件、目录、进程、IPC（来自 `mcpplibs/os`）；对话框、剪贴板、托盘（来自 SDL3） | 每个服务都标注效果分级 | M1–M3 |
| abi | 实现 `openmui.h` | 由生成器产出调用桩；不让异常越过边界 | M3 |
| player | 加载清单、视图文档、脚本、native 模块 | `entry-player` 使用它 | M3 |
| reflect | C++26 反射的易用层：`omui::form(settings)`、`omui::model_of<T>()` | **只在 GCC 16 加 `-freflection` 时可用**；C++23 下有由契约生成的替代方案 | M3 之后 |
| devtools | 检查协议、遥测、基准测试接口、像素溯源（对象 ID 缓冲）；协议操作 `clock`、`trace` 订阅、`signals` | 检查器本身就是一个 openmui 应用，同时是示例（D-61）：节点树、属性、帧时间、火焰图、日志、动画检查器、依赖图 | M2 起逐步（可观测性的数据已经在 D-59 里），M4–M6 完成像素溯源 |

C++26 反射的易用层示意（只在 `openmui-reflect` 里）：

```cpp
import openmui;
import openmui.reflect;

struct Settings { std::string name; int volume = 30; bool dark = false; };

auto settings_panel(Settings& s) -> omui::View {
    return omui::reflect::form(s);       // 用反射遍历成员，生成 TextBox / Slider / Toggle，并带上语义 ID（name / volume / dark）
}
```

---

## 6. 动画系统（重点：同时服务交互和视频）

### 6.0 定位：通用动画引擎 `hanim`（D-49）

- **独立组件 `modules/base/hanim/`**，有自己的 `mcpp.toml`，**只依赖标准库**，不知道节点、视图、渲染的存在。将来可以直接独立成仓库（甚至贡献给 mcpplibs）。
- **使用方**：`openmui.view` 把它适配到节点属性上；`openmui-video` 用它做合成和时间线；S4 游戏用它做玩法里的补间和时间倒流；画布用它驱动相机。**其他项目也可以单独使用**，比如游戏引擎、别的 UI 框架。
- **对外的核心抽象**：

```cpp
export module hanim;
import std;

export namespace hanim {

using seconds = std::chrono::duration<double>;

template <class T>
concept Animatable = requires(const T& a, const T& b, double t) {
    { lerp(a, b, t) } -> std::same_as<T>;           // 颜色在 OKLab 空间插值，变换按分量插值
};

class Clock;                                       // 交互 / 虚拟 / 渲染三种，可以注入
template <Animatable T> class Track;               // 关键帧轨道：每一段有自己的缓动
template <Animatable T> class Spring;              // 阻尼谐振子的解析解，可以分段（被打断时）
class Timeline;                                    // 轨道、序列、错峰、嵌套合成、时间映射

// 纯函数求值：任意时刻 O(1) 或 O(log n)，结果确定
template <Animatable T> auto sample(const Track<T>&, seconds t) -> T;
template <Animatable T> auto sample(const Spring<T>&, seconds t) -> T;

// 批量求值：SoA，给 UI 线程每帧调用一次
class Set;                                         // 活跃动画的集合
void evaluate(Set&, seconds t, std::span<std::byte> out);

}
```

- **和 UI 的边界**：anim 只负责“给定时刻，算出值”；“把值写到哪个属性上”由 view 负责。所以 anim 里没有任何节点、属性、线程的概念。


### 6.1 设计目标

| 目标 | 原因 |
|---|---|
| **动画是时间的纯函数** | 视频必须能**跳转到任意一帧**直接渲染，而不是从第 0 帧一路模拟过去。这样不同的帧还可以并行渲染 |
| **结果确定、跨平台一致** | 同样的时间得到同样的值（A 级）；视频每次渲染结果相同；测试可以复现 |
| **交互模式和渲染模式共用同一套动画** | 界面里调好的动效，可以原样用进视频 |
| **可以被打断，同时保持可跳转** | 交互中途改变目标时保留速度；做法是把一次动画拆成多段解析解，段的列表就是可以跳转的历史 |
| **不经过脚本** | 每帧由 C++ 批量求值；脚本只负责声明 |

### 6.2 分层

```
时钟（Clock）
  ├─ 交互时钟：真实时间，和帧节奏对齐；可以调速（慢放）、暂停
  ├─ 虚拟时钟：headless 测试按步推进
  └─ 渲染时钟：视频专用，帧号 → 时间（帧号 / fps），完全确定
时间线（Timeline）
  ├─ 轨道、序列（sequence）、串联（series）、错峰（stagger）
  └─ 嵌套的合成，带时间映射：偏移、速度、裁剪、循环、倒放
动画原语（全部有解析解，可以在任意时刻求值）
  ├─ 关键帧：每一段有自己的缓动；支持 hold
  ├─ tween：时长 + 缓动（cubic-bezier 的求解是确定的：牛顿迭代加二分，迭代次数固定）
  ├─ spring：阻尼谐振子的**解析解**（欠阻尼、临界阻尼、过阻尼三种情况），O(1) 求值；
  │          被打断时生成新的一段，初始条件就是当前的位置和速度
  ├─ decay：惯性衰减的解析解
  ├─ 路径：沿贝塞尔路径运动（按弧长参数化）
  └─ 形变：路径插值；颜色在 OKLab 空间插值
绑定：任何属性（数值、向量、颜色、变换、布局属性、着色器参数）
声明式写法：属性改变时自动过渡、出现和消失、布局动画（FLIP、共享元素）、状态机
```

### 6.3 可以动画化的属性，按代价分级

| 级别 | 属性 | 每帧代价 |
|---|---|---|
| 只需合成 | transform、opacity、着色器参数 | 只改 GPU 的 uniform 或实例数据 |
| 需要重绘 | 颜色、圆角、阴影、文字颜色 | 重新录制这一个节点 |
| 需要重新布局 | 尺寸、边距、flex 参数 | 子树重新布局（开发工具会给出提示） |

**所有可视属性都可以做动画（D-60）**：契约里类型是数值、长度、颜色、变换、渐变、阴影、路径的属性，默认都可以动画（`lanes<T>` 给出插值的分量），契约可以写 `animatable = false` 排除；生成器为每个属性产出它的代价级别，开发工具和 lint 据此提示“这个动画会触发重新布局”。颜色一律经过 OKLab，阴影、渐变逐分量插值，路径用形变插值。

**控制粒度分三层，都是时间的纯函数，都可以跳转**：

| 粒度 | 能动的东西 | 做法 |
|---|---|---|
| 对象 / 属性 | 节点的任何可视属性；布局（FLIP、共享元素）；出现和消失 | 上面的三级代价；`animating` / `removing` 进语义树，agent 可以 `wait settled` |
| 子对象 | 文字的每个字形 / 词 / 行（错峰、逐字出现、打字机）；路径的描边进度和虚线偏移（“画出来”的效果）；渐变的色标；画布和粒子的每个实例 | 文字排版后的字形序列、路径的弧长参数、实例数组都暴露为可以错峰的**域**（`stagger(域, …)`），每个元素的值仍然是 `f(t, i)` |
| 像素 | 溶解、擦除、遮罩显现、位移、噪声、模糊、色散；任意子树先渲染成图层（render-to-texture）再加效果；轨道遮罩（track matte） | Slang 效果：`f(uv, t, 参数)`，参数就是可以动画的属性；CPU 参考路径也要能算，保证金图和跳转一致性（Slang 的 CPU 目标或手写的参考实现，M4 验证）；`painter` 节点可以按时间逐像素绘制 |

**默认就能动画化，不需要组件配合**：子对象层和像素层是运行时的基础设施，作用在节点录制的显示列表上，而不是由每个组件自己实现。

- 一个节点画出的字形串（不管是 `text`、按钮的标签、Markdown 段落还是第三方组件的文字），运行时都能按节点、按文字范围（字节区间、第几个字形 / 词 / 行）拿到，并逐个应用 `f(t, i)`：位移、旋转、缩放、透明度、颜色、模糊半径。
- 任何节点都可以加 `effect` 属性：子树先渲染成图层，再在图层上逐像素求 `f(uv, t, 参数)`。内置的效果有溶解、擦除、遮罩显现、位移、噪声、色散，以及把文字或图层的像素拆成粒子再聚合。CPU 参考实现和 GPU 路径给出同样的金图。
- 文字的某个范围也可以单独成为像素层的对象，比如只让一段文字里的一个词化成粒子飞走，再在别处聚合。
- **算力**：每种效果和粒子模拟都有两条路径。
  - CPU 参考路径：确定性，用来出金图、跑 headless 和 CI；按行带并行，只算脏区。
  - GPU 路径：机器有 GPU 时，同一组效果和粒子模拟以 compute shader 运行（经 `omui.rhi`，SDL_GPU 支持 compute），按能力查询选用；它和 CPU 参考路径的金图误差不超过约定的容差。
  - 粒子天然适合 compute：一个线程算一个粒子，`f(t, i)` 是纯函数，可以直接跳到任意时刻。

这三层对视频尤其重要：Remotion 一类的工具停在 DOM / CSS 的属性层，像素层要另起 WebGL；openmui 在同一棵树、同一个时钟、同一个确定性的渲染器里覆盖三层，所以任意一帧都能直接渲染、并行渲染、增量重渲染（§12.6）。

### 6.4 API（四种语言同一套概念）

```cpp
// C++：交互里的 spring 过渡
omui::box({.id = "drawer", .x = omui::spring_to(open ? 0.f : -320.f, {.stiffness = 300, .damping = 30})});

// 时间线（交互和视频都能用）
auto tl = hanim::timeline()
    .at(0.0s, "title.opacity", hanim::tween(0, 1, 400ms, omui::ease::out_cubic))
    .at(0.2s, "title.y",       hanim::spring(40, 0, {.stiffness = 120, .damping = 14}))
    .stagger(0.05s, "cards/*.scale", hanim::tween(0.9, 1, 300ms));
```

```ts
// TS：交互
box({ id: "drawer", x: springTo(open ? 0 : -320, { stiffness: 300, damping: 30 }) });
// TS：视频里按帧求值（frame() 是 signal；interpolate 和 spring 是表达式组合函数，由原生代码求值）
text({ opacity: interpolate(frame(), [0, 30], [0, 1]), scale: spring({ frame: frame(), from: 0.8, to: 1 }) });
```

### 6.5 视频专用的能力

- **运动模糊**：渲染时在一帧内做时间超采样（取 N 个子帧再累加）。只在渲染模式下使用。
- **音频同步**：时间线驱动音频片段，按帧精确对齐（用 `compat.miniaudio` 做预览，用 ffmpeg 混音输出）。
- **跳转一致性**：直接渲染第 N 帧的结果，必须和从头播放到第 N 帧的结果**完全相同**。这是一条 CTS 用例。

### 6.6 测试

- **A 级曲线金值**：每种缓动、spring、decay 按固定的时间点采样，结果逐位比对，并且要求跨平台一致。
- **跳转一致性**（见上）。
- **被打断的 spring**：速度连续，段列表可以回放。
- **语义**：`animating` / `settled` / `removing`；`wait settled` 不会提前返回，也不会卡住。
- **减少动效**：设置开启时，spring 直接跳到终态。

### 6.7 工具

- **动画检查器**：任何正在运行的 openmui 应用都可以连接，用时间轴组件（`openmui-ui-timeline`）拖动时间、逐帧查看、慢放。
- `omui anim plot`：把一条曲线画成图，检查和设计稿是否一致。

### 6.8 强动画框架的特色（D-60）

- **时间是一等输入**：时钟可以注入、暂停、慢放、逐帧、倒放；任何应用的动画状态都可以拖到任意时刻查看（动画检查器），录下的会话可以按帧回放。
- **可以打断而且可以跳转**：spring 被打断时速度连续，段列表就是历史，所以“交互里的动画”也能跳转和回放。
- **布局动画是自动的**：尺寸和位置的变化用 FLIP，跨视图的共享元素过渡；列表的插入、删除、重排有默认的过渡。
- **设计一次，界面和视频共用**：界面里的动效 token（`motion.*`）原样进入视频时间线。
- **可测**：动画在任意时刻的值和像素都可以金值比对；`animating` / `settled` 进语义树。
- **减少动效**是全局开关，spring 直接到终态，测试覆盖。
- **代价可见**：开发工具按帧显示每个正在动的属性落在哪一级（合成 / 重绘 / 重新布局）。

---

## 7. 渲染与 GPU

| 工作 | 内容 | 里程碑 |
|---|---|---|
| raster（CPU 参考光栅器） | **自己实现定点数的扫描线光栅器**，用解析覆盖率做抗锯齿，保证跨平台逐字节一致（不依赖 pixman 的 SIMD 路径，避免不同平台的取整差异）；字形由 FreeType 光栅化（版本由 mcpp.lock 锁定，关闭 hinting） | M1 |
| render | 显示列表 → 批处理；圆角矩形、边框、阴影用 SDF；msdf 文字图集；图层缓存；脏区；外部纹理 | M4（CPU 路径在 M1 就有） |
| rhi | `omui.rhi` 接口（RHI，Render Hardware Interface）：设备、缓冲、纹理、管线、命令列表、导入外部纹理、能力查询 | M4 |
| rhi-sdl | 第一个 GPU 后端（Vulkan / Metal / D3D12） | M4 |
| shaders | 用 Slang 写内置着色器；`rules-slang` 编译成 SPIR-V / MSL / DXIL（/ GLSL ES / WGSL）；通过反射得到参数；开发期热重载 | M4 |
| 效果（第 ① 级） | 效果模板 `EffectInput`；参数就是带类型的属性，可以做动画；第三方的着色器只能使用模板 | M4 |
| 材质（第 ② 级） | 画布节点类型自己的着色器 | M4–M5 |
| rhi-gles | 嵌入式 Linux 和 WebGL2（D-36） | M6 |
| CI 中的 GPU | headless 下用 **mesa-lavapipe**（xim 中已有）跑 rhi-sdl 的 Vulkan 路径 | M4 |

---

## 8. 宿主与平台

### 8.1 平台层：git 依赖 `mcpplibs/os`

**本地事实**：`mcpplibs/os` 的 `os-0.1.0` 是一个分支。它提供进程、文件、环境变量、终端、约定目录、本地 IPC、机器信息，以及编译期的平台常量（`PLATFORM`、`is_linux` …），底层基于 openkal。它的规则 R0 到 R6 里包括：接口单元不包含系统头文件、也不包含宏；平台专属模块在所有目标上都声明，只在自己的平台上定义。

```toml
# runtime/platform/mcpp.toml
[dependencies]
mcpplibs.os = { git = "https://github.com/mcpplibs/os.git", branch = "os-0.1.0" }
```

| openmui-platform 的能力 | 来源 |
|---|---|
| 目录（配置、缓存、数据）、文件、原子写入 | `mcpplibs.os.dirs` / `.fs` |
| 打开链接、启动外部程序（效果为 `external`） | `mcpplibs.os.process` |
| 单实例、agent 端点 | `mcpplibs.os.ipc`（本地监听和连接，`single_instance`） |
| 平台常量 | `mcpplibs.os.platform`（`if constexpr` 分发） |
| 窗口、输入、输入法、剪贴板、文件对话框、托盘、显示器 | SDL3（`compat.sdl3`）——在宿主层 |

### 8.2 宿主

| 宿主 | 用途 | 里程碑 |
|---|---|---|
| **headless** | 虚拟 surface、注入时钟、输出 PNG；CI、agent、**视频渲染**都使用它 | M1 |
| **sdl3** | Linux / Windows / macOS 桌面，以后包括 Android / iOS | M1（Linux）→ M2（三个桌面平台） |
| web | emscripten（emsdk 在 xim 中已有）+ rhi-gles（WebGL2） | M6 |
| opensal | 以后接入 Luban / xdesk | 以后 |

---

## 9. 组件实施

| 组件包 | 内容和顺序 | 里程碑 |
|---|---|---|
| ui-base | Text、Box / Stack / Column / Row、Button、Toggle、Checkbox、Slider、TextBox → ScrollView、List / VirtualList、Tabs、Menu、Dialog、Tooltip、Select、Radio | M1（前 9 个）、M3（其余） |
| ui-markdown | md4c、块缓存、流式追加、扩展块、链接（`external`）、Origin | M2 |
| ui-canvas | 场景存储、空间索引、相机、LOD、晋升、实例化、`OMUI_EXT_canvas` | M4 |
| ui-canvas-kit | 选择、拖动、缩放手柄、吸附、连线、小地图、网格 | M4 |
| ui-timeline | 时间轴、播放头、轨道、关键帧标记、播放器控件 | M5（视频预览、动画检查器都用） |
| modules/ui/theme/styles | 风格包：token + Slang 效果 + 动效 token | M4（S2） |

每个组件都必须通过组件完成标准（component-binding §12）。

---

## 10. 语言绑定实施

| 语言 | 方案 | 里程碑 |
|---|---|---|
| C ABI | `openmui.h` + `modules/ui/abi`，由生成器产出 | M3 |
| TS / JS | `mcpplibs.mwrap.quickjs_ng`（quickjs-ng-m，模块封装 `compat.quickjs-ng`）；TS 在构建期用 esbuild 转换（xim 中已有）；生成 `.d.ts`；可选 TSX | M3 |
| Python | **用 CPython 的稳定 ABI（limited API / abi3）自己写绑定模块**，不用 pybind11（它是依赖宏的头文件库，不符合“不用宏、头文件最少”的规则）；先做“作为宿主”的模式（`pip install openmui`，每个平台一个 wheel）；生成 `.pyi` | M3 |
| Lua | `mcpplibs.mwrap.lua`（lua-m，模块封装 `compat.lua` 5.4）；生成 LuaLS 注解 | M3 |

- **线程**：每种语言一个逻辑线程；和 UI 线程之间用无锁的 MPSC 队列交换值。
- **测试**：同一组语言中立的 JSON 场景，分别由三种语言执行，语义结果必须完全相同（CTS 的 S 级）。

---

## 11. Agent 实施

| 工作 | 内容 | 里程碑 |
|---|---|---|
| 本地端点 | 每个应用通过 `mcpplibs.os.ipc` 在约定的目录里监听；协议是 JSON lines，接口名 `openmui.tree@0.1` | M1 |
| 命令行 | `omui agent ls / snapshot / find / act / wait / capture` | M1 |
| 效果分级 | `irreversible` / `external` 的动作没有 `answer` 时返回 `needs-answer`；actor 用 openas 的文本形式；信封 | M1 |
| 录制和回放 | 语义动作 + 时钟 → headless 下精确回放；可以转成测试 | M2 |
| MCP 服务器 | `tools/mcp`：`ui_snapshot` / `ui_find` / `ui_act` / `ui_wait` / `ui_capture`、`video_*`（§12） | M5 |
| Skills | `skills/openmui`、`skills/openmui-video`：给 Claude Code 等 agent 使用的规则包（参考 Remotion 的 agent skills） | M5 |
| 接入 Luban | 通过 git 依赖 openbus / xbus 发布到总线 | 以后 |

协议示意：

```text
→ {"op":"find","q":{"role":"button","name":"删除*"}}
← {"kind":"data","data":{"nodes":[{"id":"notes/delete","role":"button","actions":[{"name":"activate","effect":"irreversible","answer":"yes"}]}]}}
→ {"op":"act","node":"notes/delete","action":"activate","actor":"agent:planner;via=person:alice"}
← {"kind":"error","code":"needs-answer","hint":"带上 \"answer\":\"yes\" 再次调用"}
← {"kind":"result","exitCode":2}
```

---

## 12. openmui-video：用代码制作动画视频（对标 Remotion 和 Opus 5.5 带起的代码动画）

### 12.1 背景

Remotion 用 React 代码来写视频。它在 2026 年 1 月推出了 agent skill，让 Claude Code 等 agent 能用自然语言生成视频；几周之内安装量就达到十几万。它的定位是**由代码生成的动态图形视频**（产品演示、数据可视化、讲解动画），不是生成写实画面（来源见文末）。

openmui-video 走同样的路线，并且有几项明显的优势：

| 方面 | Remotion | openmui-video |
|---|---|---|
| 渲染 | 无头 Chrome 截图 | **原生 C++ 渲染**（headless 宿主 + GPU 或 CPU），不需要浏览器 |
| 确定性 | 依赖浏览器 | **动画是时间的纯函数**；CPU 渲染逐字节一致 |
| 并行 | 多个浏览器实例 | 帧之间互相独立，可以在任务线程或多个进程上**并行渲染** |
| 特效 | CSS / WebGL | **Slang 着色器**效果，和界面共用 |
| 语言 | TS / React | TS、Python、Lua、C++ |
| 给 agent 的质量检查 | 截图 | **每一帧都有语义树**：agent 可以检查文字是否重叠、是否出界、时间点是否正确，不只是看截图 |

### 12.1a 质量对标：Opus 5.5 带起的代码动画视频

2026 年 9 月 22 日 Claude Opus 5.5 发布以后，“让模型写动画代码、再逐帧渲染成视频”迅速流行：常见的流程是模型写 HTML / Canvas / three.js，再用无头 Chrome 加 ffmpeg 渲染；社区收录了上千个作品，最常见的形式是 15 秒展示短片。openmui-video 的目标是**达到同样的画面质量**，同时提供它们没有的东西：原生渲染（不需要浏览器）、结果确定、帧可以并行渲染、每一帧都有语义树可以检查。

### 12.2 概念

| 概念 | 说明 |
|---|---|
| 合成（composition） | id、宽高、fps、总帧数、默认属性 |
| `frame()` | 当前帧号，是一个 signal；只在渲染时钟下变化 |
| 序列（sequence）、串联（series） | 时间线上的片段：起点、长度、嵌套、时间映射 |
| `interpolate` / `spring` / 缓动 | 就是 §6 的动画原语，以表达式组合函数的形式提供，由原生代码求值 |
| 素材 | 图片、字体、音频、视频片段（用 ffmpeg 解码） |
| 字幕 | 文本或 Markdown 组件，加上时间轴数据 |

```ts
// intro.video.ts
import { composition, sequence, frame, interpolate, spring } from "openmui/video";
import { column, text, markdown } from "openmui";

export default composition({ id: "intro", width: 1920, height: 1080, fps: 60, frames: 600 }, () => [
  sequence({ from: 0, frames: 120 }, () =>
    text({ id: "title", text: "openmui", style: "display",
           opacity: interpolate(frame(), [0, 30], [0, 1]),
           scale:   spring({ frame: frame(), from: 0.8, to: 1, stiffness: 120, damping: 14 }) })),
  sequence({ from: 90, frames: 300 }, () =>
    markdown({ id: "pitch", source: "**Agent 原生**、可以 headless 测试的现代 C++ UI" })),
]);
```

### 12.3 渲染流水线

```
合成 + 帧区间 → 拆成帧块分给任务线程或进程 → 每一帧：渲染时钟 = 帧号 / fps
  → 对账 → 布局 → 动画求值 → 渲染（rhi-sdl 或 lavapipe，也可以用 CPU raster）
  →（可选的运动模糊：子帧累加）→ 帧缓冲
→ 编码：ffmpeg（compat.ffmpeg）→ mp4（h264 / h265 / av1）/ webm / png 序列 / gif；音频混合
```

```bash
omui video render intro.video.ts --out intro.mp4 --codec h264 --scale 1 --jobs 8
omui video still  intro.video.ts --frame 240 --out f240.png
omui video check  intro.video.ts            # 每一帧的语义检查：文字重叠、出界、空白帧
```

### 12.4 预览工具

预览工具本身用 openmui 写：时间轴可以拖动（`openmui-ui-timeline`）、播放和暂停、逐帧前进、修改属性的面板、热重载（脚本保存后立刻刷新当前帧）。

### 12.5 给 agent 的工作流

- `skills/openmui-video`：参考 Remotion 的 skill，按主题拆成很多小的规则文件（合成、动画、文字、字幕、音频、着色器效果、渲染），agent 按需加载。
- MCP 工具：`video_render(range)`、`video_still(frame)`、`video_snapshot(frame)`（某一帧的语义树）、`video_check()`。
- **闭环**：agent 写代码 → 渲染关键帧 → 读取语义树和截图检查 → 修改 → 最终渲染。

### 12.6 性能和编辑体验：要全面领先业界（D-54）

**业界现状（公开数据，口径各不相同，只作量级参考）**：

| 工具 | 公开的速度 | 编辑体验 |
|---|---|---|
| Remotion（本地） | 简单的 1080p 合成热缓存时约 90 fps；真实项目常见 5–15 fps；一份第三方指南说 60 秒的视频在本地要 3–7 分钟；首次构建还要额外几分钟，安装占用约 870 MB | Studio 预览、React 热更新；属性面板有限 |
| Remotion Lambda | 1 分钟（1440 帧）约 15–19 秒，靠的是 **约 200 个 Lambda 并发** | 同上 |
| Revideo / Motion Canvas | Revideo 自称“几乎总是快于实时”，每个渲染任务建议 8–10 GB 内存；一次对比测试里 Motion Canvas 比 Remotion 快约 35% | Motion Canvas 有编辑器，可以拖动时间轴 |
| HyperFrames | 第三方测试里本地比 Remotion 快约 2 倍 | HTML 写法 |

**openmui-video 的优势来源**：
- 原生渲染，没有浏览器、没有截图、没有进程间传帧；
- 帧是时间的纯函数，**任意帧可以并行渲染、可以缓存**；
- GPU 渲染之后直接交给硬件编码器，零拷贝（不能零拷贝的平台走固定的回读路径）；
- 每一帧都知道自己读了哪些输入，所以**可以精确地判断一次修改影响了哪些帧**。

**缓存的分层（修改代码以后只重做受影响的部分）**：

| 层 | 缓存什么 | 失效条件 |
|---|---|---|
| ① 帧依赖记录 | 每一帧读取了哪些 signal、属性、素材、着色器，以及它们在这一帧的取值摘要 | 无（每次渲染都会更新） |
| ② 图层和子树 | 静态子树的光栅结果、文字排版、字形图集、矢量路径的细分结果 | 子树的输入改变 |
| ③ 帧 | 每一帧的像素摘要（内容哈希） | 这一帧的依赖摘要改变 |
| ④ 编码分块 | 按 GOP 对齐的编码分块（例如每 2 秒一块） | 块里有任何一帧的摘要改变；没有改变的块直接拷贝码流，不重新编码 |

**性能和编辑体验的指标（参考机器：8 核 CPU + 主流独立显卡或较新的核显；具体型号在 M4 写进基准报告）**：

| 指标 | 目标 | 和业界比 |
|---|---|---|
| 完整渲染 60 秒 1080p60（3600 帧），GPU + 硬件编码 | **≤ 15 秒（≥ 240 fps，4 倍实时）** | 单机超过 Remotion Lambda 的 200 并发（1440 帧约 15–19 秒） |
| 完整渲染 60 秒 1080p60，只用 CPU 光栅器 + 软件编码 | ≤ 60 秒（≥ 实时） | 比 Remotion 本地快 3–7 倍 |
| 4K60 完整渲染（GPU） | ≤ 60 秒 | — |
| **修改一个镜头（约 5 秒）以后重新渲染整片** | **≤ 3 秒**；没有受影响的编码块 100% 命中缓存 | 业界工具都是整片重渲染 |
| 只改字幕或一个颜色 | ≤ 2 秒，只重做受影响的帧 | — |
| 冷启动到第一帧 | ≤ 1 秒（没有浏览器，没有打包步骤） | Remotion 首次构建要几分钟 |
| 预览：拖动时间轴 | 1080p 下 60fps，不掉帧；跳到任意一帧 ≤ 50 毫秒 | — |
| 预览：保存代码到画面更新 | TS / Python / Lua ≤ 100 毫秒，**时间轴位置和属性面板里的修改都保留** | — |
| 内存峰值 | 1080p 渲染 ≤ 1 GB，4K ≤ 2 GB | Revideo 建议每个任务 8–10 GB |
| 安装体积 | 命令行 + 运行时 + 默认字体 ≤ 60 MB | Remotion 约 870 MB |

**编辑体验（“可再编辑”）**：
- **属性面板写回**：在预览里选中元素（按语义 ID），拖动数值、调整关键帧、改颜色。修改写到一个旁路数据文件 `<名字>.timeline.omui.json`，叠加在代码之上，不改动代码文本；`omui video bake` 可以把它合并回代码里的字面量。人、agent、脚本修改的是同一份数据，diff 清楚。
- **像素溯源**：点击画面上的任何像素，跳到产生它的源码位置（文件和行号），以及对应的语义节点和动画轨道。
- **时间轴就是数据**：每个镜头、标记、节拍点都显示在 `openmui-ui-timeline` 上，可以拖动起点和长度；拖动的结果同样写进 `.timeline.omui.json`。
- **按时间提问**：agent 可以问“第 12.3 秒画面上有什么”，得到的是语义树，不是截图。
- **局部预览**：只渲染选中的时间段和区域（降低分辨率、只渲染一个图层），用来快速确认修改。

**基准套件**（`tests/bench/video/`，M4 建立，M5 起进门禁）：五个标准合成（文字和图形、1000 个粒子、Markdown 滚动、视频里嵌视频、着色器效果）。**在同一台机器上**分别用 openmui-video、Remotion、Revideo、HyperFrames 渲染，比较完整渲染时间、修改后重新渲染的时间、冷启动、内存峰值和安装体积。结果写进公开的基准报告。**领先的标准：每一项都优于业界最好的工具。**

---

## 13. 展示（showcase）

**核心理念（D-63）：展示不是 demo，而是融入真实功能、组件和工具里的东西——真实有用，同时用来展示。** 每个展示先回答"谁会因为它本身去用它"，再回答"它证明了哪项特色"。下面每一项都写明它的**真实身份**。

### S1 无限画布 Markdown 墙（`showcase/markdown-wall`）

- **真实身份**：**文档墙**——`omui wall <目录>` 把任意一个 Markdown 目录摊在无限画布上阅读和检索；openmui 自己的设计文档和 API 文档就用它浏览；Agent 驾驶舱（S3）的工作区也是它。
- **内容**：画布核心加上应用层自定义的 WuXian 风格交互（工作区分组、两个相机预设、小地图）；1000 个 Markdown 窗口；四个 LOD 档位；流式输出；HUD 用立即模式写。
- **验证的能力**：画布、LOD、晋升、图层缓存、任务系统、富文本、语义虚拟化。
- **指标（目标值）**：1000 个窗口在核显上缩放扫描达到 120Hz，帧时间 P99 < 8.3ms；平移缩放时每帧 CPU < 1ms；清晰化时间 < 300ms。
- **CI**：确定性时钟下的场景脚本（TS、Python、Lua 三种），性能门禁。
- **里程碑**：M4。

### S2 风格化 GPU 渲染画廊（`showcase/style-gallery`）

- **真实身份**：**风格工坊**——设计师编辑 token、Slang 效果和动效 token，在真实的界面（Inspector、设置面板、视频播放器）上实时预览，导出风格包（`*.style.omui.toml`）给真实应用使用；液态玻璃组件（D-64）在这里调参。画廊就是它的预览页，不是单独的演示程序。
- **内容**：**同一个“设置面板 + 仪表盘”应用**，可以在运行时切换五种风格包，切换时有交叉过渡：
  - **玻璃**：背景模糊、折射、高光；
  - **霓虹**：SDF 外发光、bloom、扫光；
  - **复古 CRT**：像素字体、扫描线、屏幕弯曲、荧光余辉；
  - **水墨纸张**：非真实感描边、纸张颗粒噪声、手绘抖动；
  - **液态**：开关和滑块的 metaball 形变、粘连效果。
- **风格包** = token + Slang 效果 + 动效 token，放在 `modules/ui/theme/styles/`。
- **验证的能力**：效果（第 ①、② 级）；着色器参数可以做动画；**换了风格，语义树仍然完全相同**（S 级用例）；风格不影响 agent 的操作。
- **指标**：在核显上，每种风格都达到 120Hz；效果的 GPU 开销进报告。
- **里程碑**：M4。

### S3 Agent 操作（`showcase/agent-ops`）

- **真实身份**：**`omui mcp` 和 Agent 驾驶舱**——一个真的 MCP 服务器，让 Claude Code 等 agent 操作任何 openmui 应用；驾驶舱显示 agent 的动作日志和待确认的动作。下面的笔记和文档墙任务就是它的日常用法。
- **内容**：
  - 一个 Markdown 笔记应用加上 Markdown 墙，通过 MCP 交给 agent。任务例如：“把所有带 TODO 的文档整理到一个工作区，再写一份总结”。过程中会用到语义查找和空间查询、移动、读取（内容标记为 `remote`）、删除（`irreversible`，会被拦下，需要用户确认）。
  - **agent 生成一个设置面板的视图文档**，挂载上去，再通过语义树验证结果（生成式 UI 的闭环）。
  - 整个会话被录制下来，在 headless 下精确回放，变成一个测试。
- **CI**：不需要真的调用 LLM。用一个脚本化的“假 agent”按确定的步骤操作，验证协议、效果分级和回放。
- **里程碑**：M5（基础协议在 M1 已经可用）。

### S4 特色小游戏：《组件打砖块：openmui 巡礼》（`showcase/widget-breakout`，D-50）

**定位：它是一个真正好玩的游戏，同时也是一次框架导览。** 真实身份还有一层：它是一个**确定性的 agent 评测环境**——给定种子和输入就能精确回放，agent 通过语义 API 玩它，成绩可以比较。 每一关讲解一项框架能力，而且这项能力就是这一关的**玩法本身**，不是贴在旁边的说明。

- **基础玩法**：打砖块。**每一块砖都是真实的 openmui 组件**（按钮、开关、滑块、Markdown 卡片），被击碎时播放 Slang 溶解效果，并用 GPU 实例化喷出粒子；砖块碎掉后会露出一张 Markdown 卡片，用一两句话介绍这一关演示的能力。
- **关卡（每一关对应一项框架特色）**：

| 关卡 | 演示的能力 | 玩法 |
|---|---|---|
| 1 语义 | 语义树、显式 ID | 按 Tab 打开“语义透视”，砖块显示出 role 和 ID；击碎砖块时，语义树的变化实时显示在侧栏 |
| 2 风格 | 风格包、着色器效果 | 每击碎一排，风格就切换一次（玻璃 → 霓虹 → CRT → 水墨 → 液态）；物理和语义完全不受影响 |
| 3 画布 | 无限画布、缩放 | 关卡场地本身是一块无限画布；球飞出视野时镜头会拉远，露出整面 Markdown 墙；在不同的缩放级别之间穿梭打砖块 |
| 4 时间 | 动画是时间的纯函数 | **时间倒流**：按住按键就能倒放（参考《时空幻境》），这正是“动画可以任意跳转、结果确定”这一点；道具“慢动作”直接调整动画时钟的倍率 |
| 5 Agent | agent 原生 | 和 AI 合作或对战：AI 通过语义 API 控制第二块挡板（`paddle2` 的 `move_to(x)`），它的每一个动作都实时显示在“动作日志”里 |
| 6 多语言 | 三种脚本语言、热重载 | **用户可以用 TS、Python 或 Lua 写道具**，保存后立即生效（热重载）；官方自带的三个道具，三种语言各写一个 |

- **控制**：鼠标、键盘、手柄都可以；也可以由 agent 来玩。
- **技术**：游戏循环和 HUD 用立即模式，菜单和关卡选择用保留模式；物理按固定步长计算、由种子决定；时间倒流靠 `hanim` 的分段解析解和输入记录；目标 120Hz。
- **CI**：给定种子和输入，在 headless 下回放，比对最终状态和关键帧的哈希；时间倒流和跳转的一致性；AI 关卡用脚本化的 agent。
- **里程碑**：M5。

### S5 openmui 宣传片（`showcase/promo-video`，D-51）

**完整设计见 [promo-video](2026-10-26-openmui-promo-video-design.md)：对标拆解、创意、视觉语言、配乐、分镜、制作流程、验收标准。** 要点如下：

- **定位**：框架的宣传片。片子本身用 openmui 制作，片子里演示的也是 openmui；每个镜头都是实时渲染的真实场景，不是录屏。
- **成片**：**约 60 秒的主片**（16:9，1080p60 / 4K60）+ **15 秒短片**（16:9 和 9:16）+ 中英双语字幕。
- **对标**：三部用 agent 制作、播放量达到几百万到上千万的作品：《西方文明 / PROMETHEUS》（约 1200 万到 1500 万）、Remotion 的 Agent Skills 发布视频（约 1880 万）、《I'm Upping My P(doom)》音乐动画及其翻拍（几百万）。拆解出 8 个共同点：开头 3 秒的钩子、统一的视觉语言、分章节、跟着节奏走、强烈的排版、“这是工具自己做出来的”、可以验证和复刻、结尾的口号。
- **创意**：“这个像素知道自己是什么”。从一个像素开始**一镜到底地拉远**到上千个窗口，这是无限画布独有的标志性镜头。
- **分镜（8 章，约 60 秒）**：钩子 → 标题 → 代码就是界面（四种语言变形，界面不变）→ 无限（一镜到底）→ 风格（卡点切换五种风格）→ Agent（效果分级）→ 时间（整片连同游戏一起倒流）→ 结尾（`mcpp new`，以及“由 agent 用 openmui 制作，源码见仓库”）。
- **制作**：由 agent 借助 `skills/openmui-video` 制作，人审核；**公开完整的提示历史和源码**，欢迎二次创作。
- **里程碑**：拆解、分镜、动态分镜（animatic）、风格帧在 M4；正式制作、声音、审核、发布在 M5。

### S7 特色旗舰作品（D-63）

九项特色各有一个**既真实可用、又展示能力**的旗舰作品（Inspector、`omui mcp` 驾驶舱、`omui replay`、Motion Studio 和宣传片、Perf Lab、液态玻璃组件、文档墙、组件工坊、管线工坊），每个都有招牌镜头和传播物。完整列表、招牌镜头和里程碑见 [特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md) §2。

### S6 入门和画廊（`examples/01` 到 `13`）

- **真实身份**：教程和文档的骨架；每个示例尽量是一个小而真实的工具（示例 07 是一个能用的笔记应用，同时演示 headless 测试和 agent 脚本）。
见 [canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md) §5；同时也是文档的骨架。

---

## 14. 文档计划

| 部分 | 内容 | 位置 |
|---|---|---|
| 入门 | 四种语言各一份 10 分钟教程（`mcpp new … --template openmui:*`） | `docs/{zh,en}/getting-started/` |
| 概念 | 语义树、ID 规则、效果分级、signal、verb、两种写法、一致性分级 | `docs/*/concepts/` |
| 指南 | 组件、主题和风格包、**动画**、画布、Markdown、**视频**、着色器、agent、测试、打包、多语言混合 | `docs/*/guides/` |
| 参考 | 由契约和 registry 生成的 API 参考（四种语言 + C ABI） | `docs/*/reference/`（生成） |
| 规范 | `spec/SPEC.md` 和 CTS 说明 | `spec/` |
| 给 agent 的资料 | `llms.txt`、`skills/openmui`、`skills/openmui-video`、MCP 工具说明 | 仓库根目录、`skills/` |
| 设计记录 | 设计、RFC、实施计划 | `.agents/docs/` |
| 发布 | CHANGELOG、迁移说明 | 各成员包 |

中英文双语（和 xlings 的 `docs/zh` 镜像方式一致）。示例代码直接从 `examples/` 中引用，保证文档里的代码一定能编译。

---

## 15. CI 与测试

### 15.1 测试的层次

| 层次 | 内容 | 在哪里跑 |
|---|---|---|
| 单元测试 | 每个成员的 `tests/**/*.cpp`（`mcpp test`） | 所有平台 |
| CTS 的 S / A 级 | 语义、行为、四种写法一致、三种语言一致；布局快照、动画曲线逐位比对 | 所有平台 |
| CTS 的 B 级 | CPU 金图逐字节比对；GPU 金图（lavapipe）按容差比对 | Linux（GPU 部分） |
| CTS 的 C 级 | 平台适配表（headless 下模拟各平台） | Linux |
| **跨平台确定性** | 三个桌面平台各自算出 CPU 渲染和动画曲线的哈希，必须相同 | 三个桌面平台 |
| e2e（H3） | headless 宿主 + 场景脚本 + 录制回放 | Linux |
| agent | 假 agent 的场景、`needs-answer`、回放 | Linux |
| 性能 | 帧时间 P50 / P99、分配次数、启动时间、视频渲染吞吐量；和基线比较，超出阈值就失败 | Linux（lavapipe 看相对变化；以后加自托管的 GPU 机器看绝对值） |
| 模糊测试 | 视图文档、CEL 表达式、契约、Markdown 的解析器 | 每晚 |
| Sanitizer | ASan / UBSan / TSan（任务系统和脚本线程） | 每晚 |

### 15.2 工作流

| 工作流 | 触发 | 内容 |
|---|---|---|
| `ci.yml` | PR / push | 三个桌面平台：构建、单元测试、CTS 的 S / A 级、lint（依赖方向、头文件、宏、命名） |
| `golden.yml` | PR / push | Linux：B 级金图（CPU + lavapipe） |
| `bench.yml` | PR（带标签时）/ 每晚 | 性能门禁、报告（`tests/bench`：`omui-bench --check budgets.bench.omui.json`；在 `ci.yml` 的 release 行上已经运行） |
| `nightly.yml` | 每晚 | 模糊测试、sanitizer、长时间运行、跨平台确定性比对 |
| `release.yml` | tag | 按成员发布：mcpp-index、xpkg、wheel、npm |

CI 用 `.xlings.json` 安装工具：mcpp、slang、mesa-lavapipe、esbuild、node、uv、python、emsdk、ffmpeg。

### 15.3 lint（M0 就建立）

- 依赖方向（参考 `lint_layer_deps.sh`）；
- 头文件：除 `openmui.h` 之外不允许出现头文件；C 头文件只能出现在 `native/` 里；
- **宏**：我们自己的代码里不允许出现 `#define`（`openmui.h` 是唯一的例外）；
- 命名：身份用 `openmui`，缩写用 `omui`；不允许出现裸的 `mui`；
- 可交互的节点必须有显式 ID（在 view 检查器里）。

---

## 16. 跨平台计划

| 阶段 | 平台 | 渲染 | 说明 |
|---|---|---|---|
| M1 | Linux x86_64 | CPU raster | headless + SDL3 |
| M2 | + Windows x64、macOS arm64 | CPU raster | 平台差异用模块分区处理；跨平台确定性测试开始运行 |
| M4 | 三个桌面平台 | + rhi-sdl（Vulkan / D3D12 / Metal） | lavapipe 用于 CI |
| M6 | + Web | rhi-gles → WebGL2（emsdk、openkal-emscripten） | SDL_GPU 的 WebGPU 后端还只是实验性的 PR，不依赖它 |
| M6 之后 | Android、iOS、Linux aarch64、嵌入式 Linux | rhi-sdl / rhi-gles | — |
| 以后 | MCU | mcu 渲染 | freestanding 配置档 |

---

## 17. 需要补充到生态的内容

### 17.1 已经有的（直接使用）

| 位置 | 内容 |
|---|---|
| mcpp-index | `compat.sdl3`（`mcpplibs.mwrap.sdl3`）、`compat.harfbuzz`（`mcpplibs.mwrap.harfbuzz`）、`compat.freetype`（`mcpplibs.mwrap.freetype`）、`compat.fribidi`（`mcpplibs.mwrap.fribidi`）、`compat.msdfgen`、`compat.md4c`（`mcpplibs.mwrap.md4c`）、`compat.quickjs-ng`（`mcpplibs.mwrap.quickjs_ng`）、`compat.lua`（`mcpplibs.mwrap.lua`）、`compat.ffmpeg` / `ffmpeg.ffmpeg`、`compat.miniaudio`、`compat.spirv-reflect`、`compat.libpng`（`mcpplibs.mwrap.libpng`）、`compat.zlib`、**`nlohmann.json`**（JSON，模块 `nlohmann.json`）、**`marzer.tomlplusplus`**（TOML，模块 `tomlplusplus`）、**`taskflow.taskflow`**（任务系统，模块 `tf`）、`compat.concurrentqueue`（无锁队列，`mcpplibs.mwrap.concurrentqueue`）、`compat.xxhash`、`compat.mimalloc`、`compat.wamr` |
| xim（xlings） | `slang`、`mesa-lavapipe`、`vulkan-loader` / `vulkan-headers`、`glslang`、`spirv-tools`、`esbuild`、`node`、`uv`、`python`、`emsdk` |
| mcpp-plugins | `rules-slang`、`rules-spirv`、`dist`（apk / apple / web / wix / appimage） |
| mcpplibs | `os`（git 依赖，分支 `os-0.1.0`）、`openkal` |

### 17.2 需要新增的

| 位置 | 内容 | 用途 | 时间 |
|---|---|---|---|
| mcpp-index | `compat.cpython`（limited API 的头文件和导入库） | Python 绑定 | M3 |
| mcpp-index | `accesskit-c`（预编译） | 无障碍层（D-21） | M5 |
| mcpp-index | `compat.thorvg` | Lottie（以后的扩展） | 以后 |
| mcpp-index | `compat.dawn` 或 `wgpu-native` | rhi-webgpu | 以后 |
| xim | `luarocks` | Lua 依赖（D-29） | M3 |
| xim | `ffmpeg`（命令行） | 视频的调试和转码 | M5 |
| xim | `openmui-fonts`（Inter、Noto Sans、Noto Sans CJK 子集、等宽字体） | **默认字体固定下来**，保证跨平台一致（A / B 级） | M1 |
| xim | `omui`（命令行）、`openmui-player` | 分发 | M3 |
| mcpplibs（以后贡献） | `mcpplibs.unicode`（UAX#14 / #29 的表和算法）、`openmui.raster` | 通用库 | 稳定以后 |
| mcpp | #845（入口由依赖提供）；每个成员单独打 tag 和发布（V-02）。**已确认 mcpp 支持**：按特性开关选择互斥的源文件；依赖图里有 C++26 成员时 BMI 兼容 | 工程体验 | 持续 |

---

## 18. 里程碑

> 时间按一个小团队粗略估计，要随着实际进展调整。多条工作流可以并行。

| 里程碑 | 粗略时间 | 主要交付 | 验收 |
|---|---|---|---|
| **M0 骨架** | 2–3 周 | workspace 和所有 ★ 成员的空包；registry 格式；`tools/gen` 的最小闭环（`openmui.h`、契约 → C++ 分区）；`openmui.rules` 雏形；`app-cpp` 和 `component` 两个模板；lint；CI 骨架（三个平台）；`.xlings.json`；`openmui-fonts` | `mcpp new x --template openmui` 能创建工程并构建通过；三个平台的 CI 是绿色的 |
| **M1 核心** | 6–8 周 | core（含基于 taskflow 的任务调度）/ layout（flex）/ 基础文本 / view（im）/ semantic / agent（本地端点 + 命令行）/ raster / host-headless / host-sdl3（Linux）/ ui-base 的前 9 个组件；规范 0.1 的 S 级 | 示例 01 和 07；`omui agent` 能操作示例；CPU 金图逐字节一致 |
| **M2 文本 + 动画 + Markdown** | 6–8 周（**文本可以从 M1 开始并行**） | 富文本、UAX#14、双向文字、编辑和输入法；**anim 全部功能（§6）**；ui-markdown；录制和回放；Windows / macOS | 示例 08 和 09；动画的 A 级用例和跳转一致性；跨平台确定性测试是绿色的 |
| **M3 声明式 + 三种语言** | 6–8 周 | rt / expr / 视图文档；C ABI；TS / Python（宿主模式，abi3）/ Lua；native 模块；`deps.<lang>`；player；其余模板；规范 0.2 | 示例 02–06、12；四种写法加视图文档的语义树一致；三种语言的 CTS 一致 |
| **M4 GPU + 画布 + 风格化** | 8–10 周 | omui.rhi + rhi-sdl；render；Slang 着色器流水线；效果和材质；lavapipe 进 CI；ui-canvas / canvas-kit；modules/ui/theme/styles；规范 0.3；**openmui-video 的原型，S5 的对标拆解、分镜、动态分镜和风格帧** | **S1 Markdown 墙**和 **S2 风格画廊**；第一份基准报告 |
| **M5 视频 + 游戏 + Agent** | 6–8 周 | openmui-video；ui-timeline；动画检查器；MCP 服务器；skills；accesskit | **S3、S4（六个关卡）、S5（约 60 秒主片 + 15 秒短片）**；详见 §19.3 |
| **M6 打磨和跨平台** | 持续 | 检查器（像素溯源）；性能调优和门禁；rhi-gles 和 Web；文档完整；准备公开 | 指标稳定；文档完整；评估迁移到公开仓库 |

### 18.1 M0 的任务清单（可以直接开工）

1. 根目录 `mcpp.toml`（伞包 + workspace），以及各个 ★ 成员的 `mcpp.toml` 和最小的主模块。
2. `spec/registry` 的第一批文件：roles、states、actions、effects、motion（公式）、Button / Slider / TextBox 三份契约。
3. `tools/gen`：registry 和契约 → `openmui.h`、`openmui.spec` 模块、契约的 C++ 分区（CRTP）；带单元测试。
4. `mcpp/rules`：调用生成器的 action（`role = "source"`）。
5. lint 脚本：依赖方向、头文件、宏、命名。
6. CI：三个平台的构建 + 单元测试；`.xlings.json`。
7. 模板：`app-cpp`（带一行的 `src/main.cpp`）、`component`。
8. `openmui-fonts` 的 xpkg 草稿。
9. 验证 V-02（每个成员单独打 tag 和发布）；用一个最小示例跑通“按特性选择互斥源文件”（mcpp 已经支持），作为没有宏的特性分区的样板。

---

## 19. 验收标准（D-53）

原则：**能自动检查的都进 CI**；不能自动检查的（例如视频的画面质量）写成检查清单，由维护者签字确认。数字都是目标值，M1 期间做一次实测校准，校准后的数值写回本节，从此作为门禁。

### 19.1 全局质量门禁（每个 PR、每个检查点都要满足）

| 项目 | 标准 |
|---|---|
| 构建 | Linux、Windows、macOS 三个平台都能构建（M2 起） |
| lint | 依赖方向、头文件（只允许 `openmui.h`）、`#define`（为 0）、命名、显式 ID 全部通过 |
| 测试 | 单元测试、CTS 的 S / A 级 100% 通过；B 级金图通过 |
| 确定性 | CPU 渲染和动画曲线的哈希在三个平台上完全相同 |
| 性能 | 基准测试的 P99 相对基线退步不超过 5%（超过就失败，除非 PR 里写明原因并获得批准） |
| 内存 | ASan / UBSan 每晚运行无报错；每帧零分配的模块有分配计数测试 |
| 文档 | 公开 API 的改动必须同步文档；设计文档之间互相一致 |

### 19.2 各里程碑的验收

| 里程碑 | 验收标准 |
|---|---|
| **M0** | ① `mcpp new demo --template openmui` 生成的工程能构建和运行；② 修改一份契约以后，`mcpp build` 重新生成 C++ 分区、`openmui.h`、`.d.ts` / `.pyi` / Lua 注解，并且增量构建只重新编译受影响的成员；③ 三个平台的 CI 都是绿的；④ lint 能拦下一个故意加入的 `#define` 和一个越级依赖；⑤ 用一个最小示例跑通“按特性选择互斥源文件” |
| **M1** | ① 示例 01 和 07 在 Linux 上运行；② `omui agent snapshot / find / act / wait` 能操作示例 07，不可逆动作没有 `answer` 时返回 `needs-answer`（退出码 2）；③ CPU 金图在 Linux 上两次运行逐字节相同；④ ui-base 的前 9 个组件全部满足组件完成标准；⑤ 1000 个节点的界面在 CPU 后端上 1080p 达到 60Hz（目标值） |
| **M2** | ① 示例 08 和 09；② 公开的 CommonMark 测试集通过率 ≥ 95%（GFM 扩展单独统计）；③ **动画**：§6.6 的 A 级曲线金值全部通过；跳转一致性用例（随机抽取 1000 个时刻）全部通过；被打断的 spring 速度连续（误差 < 1e-9）；④ 跨平台确定性测试在三个桌面平台上是绿的；⑤ 中文、阿拉伯语、emoji 混排的排版测试集通过；⑥ 录制一段会话，在 headless 下回放，结果完全相同 |
| **M3** | ① 示例 02–06、12；② 同一个视图用四种语言加视图文档写，语义树完全相同（CTS 的 S 级）；③ 同一组 JSON 场景由 TS、Python、Lua 执行，结果完全相同；④ 脚本热重载在 200ms 以内生效，并且 signal 的状态保留；⑤ `pip install openmui` 后运行 Python 示例（Linux）；⑥ native 模块的契约生成三种语言的绑定，不需要手写代码 |
| **M4** | ① **S1** 和 **S2** 满足 §19.3；② rhi-sdl 在三个桌面平台上运行；lavapipe 金图在 CI 里通过；③ 一个自定义的 Slang 效果从编写到生效不需要改任何 C++ 代码，着色器热重载在 500ms 以内；④ 第一份公开的基准报告 |
| **M5** | ① **S3、S4、S5** 满足 §19.3；② MCP 服务器能让 Claude Code 操作任意一个示例；③ `skills/openmui` 和 `skills/openmui-video` 发布；④ **视频基准套件（§12.6）的每一项都优于业界最好的工具**，结果写进公开的基准报告 |
| **M6** | ① Web（WebGL2）上运行 S1 的缩小版和示例 01；② 文档完整（四种语言的入门、所有指南、生成的 API 参考）；③ 所有门禁稳定运行 4 周以上 |

### 19.3 各展示的验收

| 展示 | 自动检查（进 CI） | 人工检查（维护者签字） |
|---|---|---|
| **S1 Markdown 墙** | 1000 个窗口，缩放扫描（1% ↔ 400%）：核显 120Hz 下帧时间 P99 < 8.3ms；平移和缩放时每帧 CPU < 1ms；清晰化时间 < 300ms；20 个窗口同时流式输出时不掉帧；内存不超过公开的预算；三种语言写的场景脚本结果一致 | 缩放过程中没有可见的闪烁和跳变 |
| **S2 风格画廊** | 五种风格在核显上都达到 120Hz；**换风格前后语义树完全相同**；每种风格的 GPU 开销进报告；lavapipe 金图通过 | 每种风格的观感达到设计稿的水平 |
| **S3 Agent 操作** | 脚本化的 agent 完成整理任务，结果 100% 确定；不可逆操作 100% 被拦下；录制的会话回放结果完全相同；生成的视图文档通过 schema 和语义检查 | 用真实的 LLM agent（Claude Code）手动跑 10 次，至少 9 次完成任务 |
| **S4 组件打砖块** | 120Hz，输入延迟 ≤ 1 帧；给定种子和输入，三个平台上的回放哈希完全相同；最近 30 秒内可以倒流到任意时刻，倒流后继续游戏的结果和直接回放相同；三种语言写的道具都能热重载（< 200ms）；脚本化的 agent 能通过第 5 关 | 六个关卡**每一关都能让玩家明白它演示的是哪项框架能力**；作为游戏本身好玩（试玩反馈） |
| **S5 宣传片** | 见 [promo-video](2026-10-26-openmui-promo-video-design.md) §5.1：时长（主片 60 秒 ± 5 秒，短片 15 秒 ± 1 秒）、规格、确定性、跳转一致性、逐帧语义检查零问题、字幕阅读时间、卡点误差 ≤ 1 帧、双语字幕；**渲染性能按 §12.6**：完整渲染 60 秒 1080p60 ≤ 15 秒（GPU），修改一个镜头后整片重新渲染 ≤ 3 秒 | 见 promo-video §5.2：**和三个标杆并排观看，按十个维度打分，每一项都不低于标杆的平均分，总分 ≥ 4.0**；每个镜头都在演示框架能力；制作过程有完整记录；至少 5 位外部观众看完能说出 3 个以上的特点 |

### 19.4 各模块的完成标准

| 模块类型 | 标准 |
|---|---|
| 所有成员 | 独立的 `mcpp.toml` 能单独构建和测试；README 写明用途和依赖；公开 API 有文档注释；不违反 lint |
| 核心模块（core、anim、layout、text、view、semantic、raster、render） | 有基准测试并进入门禁；热路径每帧零分配（有分配计数测试）；单元测试覆盖主要分支（M2 起统计覆盖率，目标是行覆盖率 ≥ 80%） |
| 解析器（契约、视图文档、表达式、Markdown） | 有模糊测试，每晚运行不崩溃；错误信息带位置和修改建议，并支持 `--json` |
| 组件 | 满足组件完成标准（component-binding §12） |
| 语言绑定 | 通过三种语言的 CTS；类型存根完整；示例能运行 |
| 规范 | 每一条条款都有对应的 CTS 用例；registry 能生成全部产物 |

---

## 20. 风险与对策

| 风险 | 对策 |
|---|---|
| 文本引擎的工作量被低估 | 从 M1 开始并行；复用 HarfBuzz、FreeType、fribidi；断行表由生成器产出；尽早建立测试集（CJK、双向文字、emoji、组合字符） |
| GCC 16 的反射还是实验性的 | 反射只放在 `openmui-reflect`，核心不依赖它；每一处都有 C++23 下的替代方案 |
| 浮点结果跨平台不一致 | `-ffp-contract=off`、禁用 fast-math、自己写定点光栅器；跨平台哈希测试从 M2 开始运行 |
| 自己维护 GPU 抽象层的成本 | 范围由渲染特性的需要决定（D-81，2026-10-28 修订；原来是“严格控制在 UI 需要的程度”）；每项能力进 rhi.cts；第一个后端是 SDL_GPU 的薄映射；每个后端都跑 B 级 CTS |
| 视频渲染的性能 | 帧之间互相独立，可以并行；GPU 路径；编码交给 ffmpeg |
| 范围太大 | 每个里程碑都绑定展示作为验收；“非目标”写进文档；S1 到 S5 之外的展示一律推迟 |
| 依赖 mcpp 尚未提供的能力 | #845 有过渡办法；其余需求提前列出，和 mcpp 一起推进 |
| 只有一个维护者 | 成员可以独立；规范先行；文档和 skills 让 agent 也能参与开发 |

---

## 21. 本计划新增的决策

| ID | 决策 |
|---|---|
| D-38 | **不用宏，用 constexpr**：平台和特性用“清单按目标选择模块分区 + constexpr 常量 + `if constexpr`”；编译期校验用 `consteval`；C 宏在绑定模块里转换；只有生成的 `openmui.h` 是例外 |
| D-39 | C++23 是基线；C++26 特性（反射等）只在单独的成员 `openmui-reflect` 里使用（GCC 16，`-freflection`），核心不依赖它 |
| D-40 | 全项目只有一个头文件 `openmui.h`（C ABI）；其他全部是模块；第三方头文件只出现在 `native/` 实现分区里 |
| D-41 | 一个仓库，多个 workspace 成员；每个成员有独立的 `mcpp.toml`、版本、测试，只通过 `path` 依赖互相引用，可以随时拆成独立仓库 |
| D-42 | OS 服务通过 git 依赖 `mcpplibs/os`（分支 `os-0.1.0`）；窗口和输入走 SDL3 |
| D-43 | CPU 参考光栅器自己实现（定点数、解析覆盖率），保证跨平台逐字节一致 |
| D-44 | 动画是时间的纯函数（spring、decay 都用解析解），可以任意跳转、结果确定；交互模式和渲染模式共用；核心动画提前到 M2 |
| D-45 | 新增 openmui-video（用代码制作动画视频，对标 Remotion）和配套的 agent skill |
| D-46 | Python 绑定用 CPython 的稳定 ABI（abi3）自己实现，不用 pybind11 |
| D-47 | UAX#14 断行表由 `tools/gen` 从 Unicode 数据生成，不依赖 ICU |
| D-48 | 默认字体通过 `openmui-fonts` 固定下来，作为渲染确定性的一部分 |
| D-49 | **通用动画引擎 `hanim`**：独立组件 `modules/base/hanim/`，只依赖标准库，不知道 UI 的存在；UI、视频、游戏、画布共用；将来可以独立成仓库 |
| D-50 | S4 改为《组件打砖块：openmui 巡礼》：六个关卡，每一关的玩法本身就是一项框架能力（语义、风格、画布、时间倒流、agent、多语言） |
| D-51 | S5 是 **openmui 宣传片**：约 60 秒主片 + 15 秒短片，双语字幕；用 openmui-video 制作，全部是实时渲染的真实场景；对标三部播放量几百万到上千万的 agent 代码动画视频，拆解、分镜、验收见 promo-video 设计文档 |
| D-52 | 协作流程：单个 PR，每一轮修改一个 commit 作为检查点 |
| D-53 | 每个里程碑、展示、模块都有验收标准；能自动检查的进 CI，不能自动检查的写成检查清单并由维护者签字 |
| D-55 | **独立组件放在 `modules/` 下**（参考 mcpp 的 `modules/`）：它们不依赖 openmui 的任何其他包，随时可以拆成独立仓库；openmui 专用的包留在 spec / runtime / hosts / components 等原来的位置 |
| D-57 | **渲染层抽象也是独立组件**：`modules/gfx/render`（显示列表、渲染器接口、CPU 和 GPU 后端、内置着色器），`gfx/` 目录取消；布局引擎 `modules/content/layout`、图像编解码 `modules/base/image`、录制回放 `modules/base/session`、文件和视频输出 `modules/encode` 同样放在 `modules/` 下；这些模块共用一个只依赖标准库的几何模块 `modules/base/geom`（规范保留自己的线上值类型，运行时在边界处转换）。**平台相关的代码一律经由 `mcpplibs.os`**（文件、目录、进程、管道、IPC），窗口和输入经由 SDL3 |
| D-58 | **GPU 抽象层叫 RHI**：`modules/gpu` 改名为 `modules/gfx/rhi`（包 `openmui-rhi`，接口 `omui.rhi`，后端 `rhi-sdl`、`rhi-gles`、`rhi-webgpu`、`rhi-vk`），沿用业界对“渲染硬件接口”的叫法；`modules/gfx/render` 保持原名：它是渲染子系统（显示列表、渲染器接口、CPU 和 GPU 渲染器），和 `layout`、`raster`、`text` 一样以子系统命名 |
| D-59 | **可观测性是框架的一部分，不是外挂工具**：独立组件 `modules/base/trace`（`openmui.trace`：按线程的定长缓冲，每个事件无锁、无分配；关闭时一个 span 只是一次 relaxed 原子读；导出 Chrome trace JSON，在 Perfetto 里打开；按 span 汇总 count / total / p50 / p95 / p99 / max）；日志分类别（`Log{"layout"}`），过滤规则 `layout=debug,*=info` 来自 `OMUI_LOG` 或 `--log`，最近的记录留在环形历史里；运行时记录每一帧各阶段的耗时（timers、input、regions、animations、layout、bounds、paint）和最近 240 帧的历史，慢帧按限频写警告；调试模式（`DebugOptions`）叠加节点边界、重绘闪烁、悬停检查器（盒模型、路径、类型、源码行）和帧时间 HUD；agent 协议增加开发操作 `inspect`、`stats`、`profile`、`logs`、`debug`，`omui agent` 和 `omui::run` 的 `--debug` / `--log` / `--profile` 对应它们。开发辅助只走诊断通道，不改变语义树，像素黄金值只在调试模式关闭时有效 |
| D-60 | **所有可视属性都可以做动画，控制粒度到像素**：契约里数值、长度、颜色、变换、渐变、阴影、路径类型的属性默认可以动画，代价级别由生成器标出；粒度分对象 / 属性、子对象（字形、路径弧长、实例，作为可错峰的域）、像素（Slang 效果、图层、轨道遮罩、按时间绘制的 `painter`）三层，全部是时间的纯函数，可以跳转；颜色一律在 OKLab 插值（§6.3、§6.8） |
| D-61 | **开发工具用 openmui 自己写，同时是示例**：`tools/inspector`（openmui-devtools）通过 agent 协议连接任何正在运行的应用（进程外，几乎不扰动目标），包含节点树和属性面板（`inspect`）、帧时间曲线和阶段堆叠图（`stats`）、火焰图和时间线（`profile` 的 trace，画布 + 虚拟化，10 万个事件流畅）、日志查看（`logs`）、叠加层开关（`debug`）、动画检查器（拖动目标应用的时钟）、像素溯源（对象 ID 缓冲）和“为什么重绘 / 重新布局”的依赖图；它自己的界面也在 headless 下做金图测试，可以检查它自己；作为示例展示画布、虚拟化、时间轴、图表和 agent 协议。需要的协议操作：`clock`（暂停、逐帧、跳转、调速）、`trace` 流式订阅、`signals`（依赖图）。**每个工具都有两种界面，读同一份数据**：命令行（JSON 行输出、退出码，方便 agent 分析和 CI）和用 openmui 写的界面（方便人观察和操作）；每个工具的界面都要**突出框架自己的特色**，让工具本身就是框架的展示：火焰图和事件时间线用画布、虚拟化和动画缩放，节点树用语义树和 agent 协议，动画检查器用可跳转的时钟，基准报告用仪表盘（`tests/bench` 的参考场景本身就是 1000 个节点的仪表盘）。`omui-bench`、`omui agent` 先有命令行，界面随 inspector 落地 |
| D-62 | **增量帧是精确的**：`modules/gfx/render` 的 `DamageTracker` 按节点标记把显示列表分段（段的哈希覆盖自己的命令和子节点的顺序，范围在设备坐标里），和上一帧比较，给出可能变化的**整行带**；CPU 光栅器保证一个行带和整张画面的同几行逐字节相同（行边界的交点总是从边自己的端点算，越界的行跳过而不是截断），所以只画这些行就等于完整渲染。测试对每一种变化逐字节比对；M1 ⑤ 的仪表盘单个文字变化的一帧从 28ms 降到 2ms 以内。行带还可以切成任务在线程池上同时画（每个任务一个画布，字形先在调用线程上解析），结果仍然和串行渲染逐字节相同；所有卡片同时动画的一帧从 25.7ms 降到 5ms。GPU 路径用同一个 tracker 做 scissor。光栅器在复用画布时每帧不分配 |
| D-63 – D-67 | 双视角作品、液态玻璃效果组件、可拔插架构（契约⑦）、管线编排、工具共用外壳：见 [特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md) §5 |
| D-56 | **mcpp-index 里已经有知名、跨平台的库时，优先使用生态里的库，不自己实现**：JSON 用 `nlohmann.json`，TOML 用 `marzer.tomlplusplus`，任务系统用 `taskflow`，无锁队列用 `compat.concurrentqueue`，PNG 用 `compat.libpng`，Markdown 用 `compat.md4c`，排版用 HarfBuzz / FreeType / fribidi。**只有两种情况自己实现**：生态里没有（UAX#14 断行表）；或者确定性要求无法满足（anim 的数学函数，因为各平台 libm 的结果不同；CPU 光栅器，因为 pixman 的 SIMD 路径在不同平台上取整不同）。缺少的库优先补充到 mcpp-index（§17.2） |
| D-54 | openmui-video 的渲染速度、修改后的增量渲染（四层缓存）、GPU 和硬件编码、预览和可再编辑性都作为指标，**每一项都要优于业界最好的工具**（§12.6）；修改以旁路数据文件 `.timeline.omui.json` 叠加在代码之上 |

---

## 22. 实施进度（截至 2026-10-27）

按本计划从 M0 开始实施，在 PR #1（分支 `openmui-design`）上每轮一个提交。本节只记录事实：做到了什么、凭什么说做到了、还差什么。

### 22.1 工程约定（全部在执行，lint 或 CI 检查）

- **`.cppm` + `.cpp` 分离**：每个模块的接口单元只放声明和必要的内联，实现放在 `.cpp` 实现单元；第三方头文件只出现在 `native/` 实现分区。
- **mcpp 和 C++ 规范**：以 mcpp 的 `.agents/skills` 为准，逐条落到 openmui：

  | mcpp skill | 规则 | openmui 的做法 |
  |---|---|---|
  | `mcpp-contributing` §项目结构 | `modules/` 放独立包，各带 `mcpp.toml`、由 `path` 引用、依赖显式声明；子系统的单测放 `modules/<x>/tests/`（只在它自己声明的依赖下构建，能抓到“偷偷依赖了没声明的东西”） | 每个目录一个成员；`modules/*` 不依赖 openmui 的任何包（lint `modules`、`layering`）；每个成员的测试在自己的 `tests/` 里 |
  | `mcpp-contributing` §注意事项 | 平台相关代码集中在一处，不在别的模块里写 `#if defined`；import 指向类型的提供者，而不是它的某个消费者 | 平台代码只经由 `mcpplibs.os`（lint `platform`），没有宏（lint `macros`）；分区和模块按提供者导入 |
  | `mcpp-contributing` §路径窄化不变式 | `std::filesystem::path::string()` 在 Windows 上经过 ANSI 代码页，遇到拼不出的名字就抛异常，而 Linux / macOS 上永远不失败；比较用 `path`，身份和显示用 UTF-8 | `omui::utf8()` / `generic_utf8()`；文件经由 `path` 本身打开；lint `paths` 禁止测试以外的 `string()` / `generic_string()`（`// NARROW-OK:` 写明理由才放行） |
  | `mcpp-contributing` §PR 流程 | 永远走 PR；CI 全部通过 | 所有工作在 PR #1 上，每轮一个提交；7 个 CI 作业加每晚的 sanitizer 全部通过后才算完成一轮 |
  | `mcpp-usage` | `mcpp.toml` 的写法、workspace、`mcpp test -p`、工具链 | 只用 `mcpp.toml`；带命名空间的依赖写成点分键（lint `manifest`）；`mcpplibs.os` 按版本 `0.1.0` 从 mcpp-index 引用 |
  | `mcpp-docs-style` | 文档描述已经实现的东西，陈述式、准确、无表情符号，与实现一致 | 成员的 README 只写已实现的行为；设计记录和实现计划随实现更新（本节） |
- **不降级、不推迟**：遇到上游缺口（mcpp-index 的包、工具链差异）就在上游修好再继续，而不是绕开或降低检查（mcpp-index #571、#575、#576、#577）。
- 不用宏（唯一例外 `openmui.h`），不用异常和 RTTI（lint `exceptions`），每帧零分配有 bench 预算检查。

### 22.2 全局质量门禁（§19.1）

| 项目 | 状态 | 依据 |
|---|---|---|
| 构建：三个平台 | ✅ | GitHub Actions：Linux x86_64（GCC 16 dev / release、LLVM 23）、Linux aarch64（LLVM 23）、macOS 15、Windows 2022（MSVC ABI）全部通过（提交 `a65e61b`） |
| lint | ✅ | `tools/lint/lint.py`：10 条规则，自测 10/10 能拦下各自的违规 |
| 测试 | ✅ | 36 个成员、54 个测试程序在所有平台通过 |
| 确定性 | ✅ | CPU 渲染和动画的固定哈希在 Linux、macOS、Windows（MSVC 和 MinGW）上完全相同；增量帧与完整渲染逐字节相同（D-62） |
| 性能 | ✅ | `tests/bench`：12 项预算在 CI 的 release 行检查并保存报告 |
| 内存 | ✅ | 每帧零分配有分配计数（bench `max_allocations`）；`sanitize` 配置（ASan + UBSan，整个依赖图都插桩，第一个问题即失败）每晚在 CI 上运行（`nightly.yml`，LLVM：GCC 16.1 的 UBSan 空指针检查在 `import std` 的 `std::variant` 上会让编译器崩溃）；本地全部成员通过，发现并修好了 SDL 软件窗口加载 GPU 驱动（驱动在退出时泄漏） |
| 文档 | ✅ | 每个成员有 README；决策 D-59 至 D-67 已记录 |

### 22.3 里程碑验收（§19.2）

| 里程碑 | 条目 | 状态 |
|---|---|---|
| M0 | ① 模板生成的工程能构建和运行 | ✅ `tools/ci/templates.sh`（app-cpp、component） |
| | ② 改契约后重新生成 C++ 分区等，增量构建 | ✅ `openmui.rules` 构建插件；`tests/rules` 夹具；生成物有新鲜度测试 |
| | ③ 三个平台的 CI 是绿的 | ✅ 见 22.2 |
| | ④ lint 拦下 `#define` 和越级依赖 | ✅ lint 自测 |
| | ⑤ 按特性选择互斥源文件的最小示例 | ✅ `tests/features`：两个特性各自提供分区 `openmui.features:sink`，常量加 `if constexpr`，没有宏；一个工作区外的应用在依赖边上选择另一个（工作区构建会把所有成员一起规划，同一个包的两种互斥配置不能同在一次构建里）；两个同时启用会在扫描时被拒绝（`tools/ci/features.sh`） |
| M1 | ① 示例 01、07 在 Linux 上运行 | ✅（并且在所有平台的 CI 上测试） |
| | ② `omui agent` 操作示例 07，不可逆动作返回 `needs-answer` | ✅ `tools/ci/agent-e2e.sh`，单独进程 |
| | ③ CPU 金图两次运行逐字节相同 | ✅ 并且跨平台相同 |
| | ④ ui-base 前 9 个组件满足完成标准 | ✅ 14 个组件，契约优先，金图 |
| | ⑤ 1000 个节点、1080p、CPU 后端 60Hz | ✅ GitHub 的 4 核机器上单个文字变化的一帧 p95 2.9ms，全部卡片动画 p95 10ms |
| M2 | ① 示例 08 和 09 | ✅ `examples/08-markdown-viewer`（目录、文档间链接和返回、外部链接 S-2a、保存即重载）和 `examples/09-markdown-stream`（标准输入流式渲染，新字节淡入）：都是真实工具，headless 测试，像素在各平台固定 |
| | ② CommonMark 测试集通过率 ≥ 95%（GFM 扩展单独统计） | ✅ `modules/content/markdown`（md4c 经 `mcpplibs.mwrap.md4c`）：CommonMark 0.31.2 规范 652/652，GFM 扩展、回归和覆盖用例 156/156；808 个例子从文档模型写出的 HTML 和 md4c-html 逐字节相同；三个桌面平台和 wine 上一致 |
| | ③ 动画：A 级曲线金值；随机 1000 个时刻的跳转一致性；被打断的 spring 速度连续（< 1e-9） | ✅ 曲线、spring、decay 的金值在各平台固定；通道保留被打断前的分段作为历史（30 秒），事后跳转到 1000 个随机帧和播放时逐位相同，打断处速度连续；运行时跳转越过被打断的过渡也一样 |
| | ④ 跨平台确定性测试在三个桌面平台上是绿的 | ✅ 每个金图（视图、组件、文本、效果、示例）在 Linux / macOS / Windows（MSVC ABI）和 wine 上是同一个固定哈希 |
| | ⑤ 中文、阿拉伯语、emoji 混排的排版测试集 | ✅ `modules/content/text`：回退、双向、断行、字素和混排像素固定；带样式的字形（旋转、着色）也固定 |
| | ⑥ 录制一段会话，在 headless 下回放，结果完全相同 | ✅ 示例 07 的“recorded session replays exactly”；会话文件名可以是任何文字（`mcpplibs.os` 0.1.1） |
| M3 | ① 示例 02–06、12 | ✅ 02 hello-ts、03 计数器（五种写法）、04 阅读清单（应用自己的组件）、05 对比度检查器（应用自己的 native 模块）、06 主题预览、12 笔记（四种语言）：都是真实工具，headless 测试，像素在各平台固定 |
| | ② 四种语言加视图文档的语义树相同（CTS 的 S 级） | ✅ 示例 03：C++、视图文档、Lua、TypeScript、Python 五种写法的语义树和像素完全相同；示例 04 的组件从 Lua、视图文档、C++ builder 得到同一个节点 |
| | ③ 同一组 JSON 场景由 TS、Python、Lua 执行，结果相同 | ✅ `spec/conformance/S` 的五个场景（counter、notes、form、finder、themed），三种语言的轨迹和固定轨迹逐条相同 |
| | ④ 脚本热重载 200ms 以内，signal 状态保留 | ✅ Lua、TypeScript、视图文档的重新加载测试（节点和 signal 保留） |
| | ⑤ `pip install openmui` 后运行 Python 示例（Linux） | ✅ `tools/ci/python-wheel.sh`：装好的 wheel 上跑套件、示例 03 和 `app-py` 模板，`mypy --strict` |
| | ⑥ native 模块的契约生成三种语言的绑定 | ✅ `omui-gen native` + `openmui.rules` 的 `natives()`；`fuzzy` 和示例 05 的模块，CTS 场景 `finder` |

### 22.4 计划之外提前完成的

- 可观测性（D-59）：`modules/base/trace`、日志类别、帧阶段耗时、调试叠加层、agent 开发操作。
- 精确的增量帧和并行行带（D-62），稀疏光栅化（全帧 22.8 → 8.0ms）。
- 所有可视属性可做动画（D-60），OKLab 颜色；运行时的时间控制（暂停、单步、调速、跳转）。
- 管线编排（D-66）：`modules/base/pipeline`，运行时一帧是八个阶段插件，agent 可以在运行中替换。
- F1 / F4 的旗舰作品 Inspector 第一版（D-61、D-63）。
- 细粒度动画的基础设施（D-68 阶段 ①–③）：
  - 字形带来历（源字节、字素、词、行），绘制命令可以带逐字形样式。
  - 任何节点的 `glyph_motion`（逐字形 / 字素 / 词 / 行，错峰、带种子的随机、重复；预设：打字机、升起、聚合、散开、波浪）。
  - 效果注册表（溶解、擦除、显现、位移、噪声、色散、模糊、像素化、粒子，以及程序自己注册的效果），任何节点的 `pixel_effect`。
  - 全部是时间的纯函数：能跳转，在行带并行和增量 damage 下逐字节一致，三个平台金图一致。
  - 组件不需要配合：Markdown 组件的文字直接就能动。GPU 路径（阶段 ④）在 M4。
- Markdown 的文档模型（`modules/content/markdown`）：块和行内标记、标题锚点、任务位置、块哈希；流式追加只重新解析尾部（稳定边界由 md4c 用哨兵行确认，结果总等于整篇解析；4 字节一个 token 时每次约 1µs）。md4c 按 mwrap 规范做成了 `mcpplibs/md4c-m`（`mcpplibs.mwrap.md4c` 0.5.3，含 md4c-html 和宏映射，上游 808 个例子在四个平台的 CI 上通过），已收录进 mcpp-index（#578）。框架用到的 compat 库都按 mwrap 规范做成模块封装仓库（每个仓库一个 PR，测试和四个平台的 CI，上游自己的测试集跑在用模块写的程序上）：`mcpplibs/fribidi-m`（Unicode BidiTest 770,241 例、BidiCharacterTest 91,707 例全过，index #593）、`mcpplibs/freetype-m`（218 个函数全部导出，index #594，顺带补全 compat.freetype 漏编的部分 #589 / #591）、`mcpplibs/harfbuzz-m`（HarfBuzz 自己的 7,549 个排版用例全过，index #597，修正 compat.harfbuzz 缺的 FreeType 变体开关 #590）；`mcpplibs/libpng-m`（libpng 自己的 pngtest 移植到模块，96 个用例的输出和上游逐字节相同，index #592）、`mcpplibs/concurrentqueue-m`（上游 45 个单元测试对着模块全过，index #596）。`mcpplibs/sdl3-m`（2795 个声明导出，SDL 自己的 testautomation 26 套 377 个测试编译成 C++ 对着模块全过，index #595）。`modules/content/text`、`modules/base/image`、`modules/ui/core`、`modules/ui/host/sdl3`、`modules/gfx/rhi/sdl` 都改为 `import mcpplibs.mwrap.*;`：openmui 自己的源码里除了 `openmui.h`，不再包含任何第三方头文件，清单里也不再直接依赖 `compat.*`。（Windows 的 clang 在一个单元里同时导入 taskflow 和另一个模块时，taskflow 各分区的内部函数会重名：线程池和 UI 队列分成两个实现单元。）

### 22.5 还差什么（按计划顺序）

- M2 的验收已完成（见 22.3）。
- M3：
  - ✅ 表达式 `openmui.expr`（`modules/ui/expr`）：CEL 兼容的解析器和打印器（往返由随机合法和随机非法输入做模糊测试，四个平台同一个固定哈希）、组合子、同步求值器（错误给 null 和定位诊断，带修复提示）、注册表里的全部函数（函数表从生成的注册表来）；树高不超过 200 层，解析、求值、打印都不会栈溢出。`openmui.unicode` 同时补上了完整的大小写映射（SpecialCasing、Final_Sigma）。
  - ✅ 视图文档 `openmui.viewdoc`（`modules/ui/viewdoc`）：检查（名字、类型、ID、表达式，带位置和修改建议）、加载成和 builder 相同的 Element、绑定和事件（`set` / `verb` / `emit`，激活上的 verb 走 S-1 / S-2）、`if` / `for`（列表里虚拟化）、插槽、原地重新加载（signal 和节点保留）、组件文档（接口生成契约）、JSON Schema（`spec/schema/view.schema.json`，CI 校验）、规范格式；`omui check` / `fmt` / `schema`；随机文档模糊测试。
  - 示例 03 的 C++ 和视图文档两种写法：语义树和像素完全相同（S 级），保存即重新加载（< 200ms，计数保留）；TS / Python / Lua 版本随绑定加入。
  - ✅ C ABI `openmui.abi`（`modules/ui/abi`）：`openmui.h` 的全部函数，视图文档、节点、signal、verb（S-1 / S-2）、总线接口（snapshot / act / wait）、帧和像素；类型和声明由 `omui-gen abi` 从 `abi.toml` 生成，原生单元对照头文件检查每个大小和偏移；C++ 测试覆盖每个函数和每种误用，C 程序测试头文件本身。为绑定追加了 `omuiCheck`、`omuiDefineComponent`、`omuiSignalFind`、`omuiSignalWatch`、`omuiRun`。
  - ✅ 脚本绑定的公共层 `openmui.lang`（`lang`）：脚本线程、视图编译成视图文档、signal 镜像、处理函数转发、脚本实现的 verb（不会死锁）。
  - ✅ Lua 绑定 `openmui.lang.lua`（`lang/lua`）：表构造的 builder、signal 和表达式、区域、verb、model、语义树；Lua 写的计数器和手写的视图文档是同一棵树（S 级）；热重载保留状态，200ms 以内。
  - ✅ TS / JS 绑定 `openmui.lang.js`（`lang/js`）：QuickJS-ng（`compat.quickjs-ng` 补齐了 macOS 和 Windows，mcpp-index #582），TypeScript 由 esbuild 编译。
  - ✅ 播放器 `openmui.player`（`modules/ui/player`）和 `entry-player` 特性：清单里的组件文档、视图文档和脚本（语言是插件），保存即原地重新加载。
  - ✅ 示例 03 五种写法（C++、视图文档、Lua、TypeScript、Python）语义树和像素完全相同。
  - ✅ Python 绑定（`lang/python` + `src/entry/python`，abi3，宿主模式）：扩展 `_openmui` 不用 CPython 头文件、不链接 libpython，函数在加载它的解释器里解析（`mcpplibs.os` 0.1.2 的 `library`），一份构建服务一个平台上所有 CPython 3.10+；`with` 块 builder、运算符表达式、verb 装饰器、异常；`.pyi` 从组件目录生成（`mypy --strict` 通过）；PEP 517 后端（`lang/python/packaging`）用 mcpp 构建、按 mcpp 的结果记录取产物、捆绑旁边的共享库和第三方声明，归档可复现；CI 里 pip 安装 wheel 后跑套件和示例 03（M3 ⑤）。为此修了 mcpp：kernel-abi 包现在会放进引用它的共享库（mcpp-community/mcpp#846）。
  - ✅ CTS 的 S 级场景（M3 ③）：`spec/conformance/S` 里语言中立的场景（视图、处理函数、verb、步骤；`spec/schema/scenario.schema.json`，lint 校验），Python、Lua、JavaScript 各有一个运行器（`lang/*/tests/cts.*`），用各自的 builder、运算符和闭包搭视图，用各自的 API 执行步骤，记录的轨迹必须和场景旁边固定的轨迹逐条相同（`openmui.cts` 是 C++ 一侧的驱动）。三种语言一次就对上了；顺带补上了：`each` 的 `where` 可以是行的函数（三种语言），动作的参数按声明检查（缺少、类型不对、多余都以 `invalid-argument` 拒绝，规范 §3.3，参数类型可以写成 `f64 | min | max` 这样的备选），Session 把拒绝作为记录返回。
  - ✅ native 模块（M3 ⑥）：`*.native.omui.toml` 契约 → `omui-gen native`（记录类型、要实现的函数声明、转换、`define()`；TS / Python / Lua 存根）→ 注册表 `openmui.native`（参数按契约类型在一处检查，`invalid-argument` 指出位置）→ C ABI `omuiNativeCatalog` / `omuiNativeCall` → 三种语言的绑定由目录生成（`openmui:native/<name>`、`openmui.native`、`omui.native`；async 的在 worker 上跑，JS 得到 promise，另有 `…Sync`）；`openmui.rules` 的 `natives()` 在构建时生成。官方 native 模块 `fuzzy`（`modules/base/fuzzy` 是独立的 fzf 式匹配算法，确定、多线程排序）由各宿主注册；CTS 场景 `finder` 证明三种语言经生成的绑定得到同一轨迹。示例 05 是 WCAG 对比度检查器：TS 界面 + 应用自己的 native 模块（C++ 色彩科学），构建时生成绑定。
  - ✅ 主题即数据：`modules/ui/theme`（`openmui.theme`）读 `*.theme.omui.toml`（主题、`extends`、覆盖层 overlay；颜色、文字、间距、圆角、阴影、动效 token、`density`），一次读完报告全部错误（行列和修改建议），组合 `["dark", "compact", "reduced_motion"]`；`modules/ui/theme/default`（light / dark / reduced_motion，与运行时内置 token 由测试保持一致）、`modules/ui/theme/compact`（密度 0.8，基础控件的高度和内边距按密度缩放，默认密度下所有金图不变）；文件由通用的 `omui-gen embed` 嵌入；C ABI `omuiTheme` / `omuiThemes`，三种语言的 `theme()` / `themes()`；播放器清单的 `themes` / `theme`，保存即重新加载；CTS 场景 `themed` 证明各组合下语义树相同。示例 06 是 Lua 写的主题预览工具（应用自己的品牌主题文件热重载）。
  - ✅ 示例 04 自定义组件：阅读清单（TS），应用自己的 `rating` 组件从契约（`rating.contract.omui.toml`）经 `openmui.rules` 构建时生成 C++ 分区、builder 和 TS / Python / Lua 存根，`src/rating.cpp` 写行为（星形逐渐填充、悬停预览、动作和键位）；注册后 TS、Lua、视图文档和 agent 都按名字使用，测试证明 Lua / 视图文档 / C++ builder 得到同一个语义节点。
  - ✅ 模板：`app-ts` / `app-lua`（播放器跑脚本，headless 测试像 agent 一样按按钮）、`app-py`（`pip install openmui` 的纯 Python 应用，CI 在装好的 wheel 上跑它的测试并做 `mypy --strict`）、`native`（native 模块包，构建时生成绑定）、`theme`（主题包，`openmui.rules` 的 `themes()` 在构建时嵌入主题文件）；`tools/ci/templates.sh` 全部渲染、构建、测试。
  - ✅ `modules/ui/reflect`（`openmui.reflect`，C++26，GCC 16 `-freflection`）：`form(signal)` 按成员类型生成文本框 / 开关 / 滑块 / 下拉 / 分组，双向绑定，成员名是语义 ID 和标签，注解 `range` / `label` / `hidden`；`to_value` / `from_value` / `fields<T>()`。C++26 的模块要和 C++23 的分开构建，所以它单独成员（V-07：注解里的字符串要用数组，字符串字面量的地址不是常量）。**V-08：GCC 16.1 不能在 openmui 的依赖图里构建它**：libstdc++ 的 `std` 模块只有带 `-freflection` 构建时才导出 `<meta>`，而这样构建的 `std` 和任何在全局模块片段里包含 C 头文件的单元（libpng、FreeType、SDL 和第三方模块包装）合并不了 C 库的匿名结构体 typedef（先是 `__mbstate_t`），不带 `-freflection` 的单元又读不了它；只给本包的单元加 `-freflection`、把 `<meta>` 单独包成模块都实测无效，Clang 23 没有 `-freflection`。代码只在独立的小依赖图里验证过；CI 的 `tools/ci/reflect.sh` 先构建最小的冲突图，失败时给出警告，工具链修好后就运行本包的测试并要求通过。
  - ✅ 示例 12 笔记：TypeScript 写视图和逻辑，Lua 写文本宏（verb，任何语言和 agent 都能调用），视图文档是帮助面板，C++ 的 `fuzzy` 边输入边在 worker 上搜索；它们只共享 signal、model、verb 和事件。为此补上了：
    - 出口（outlet）：没有代码填的插槽显示以它的名字挂载的视图，有人读这个插槽时视图移进来，没人读时回到顶层。
    - 事件在批次之后送达（core 的 `after_batch`）：脚本的处理函数读到的 signal 就是事件发生时的值。之前绑定的镜像晚一步，清空搜索框时搜的是上一次的文字。
- M3 的验收已完成（见 22.3）。
- M4：`modules/gfx/rhi` 和 `rhi-sdl`、GPU 渲染路径、Slang 着色器、效果和背景输入（液态玻璃，D-64）、画布、风格包、S1 / S2。进行中：
  - ✅ `omui.rhi`（描述先校验再交给后端，命令列表是数据）、`openmui.shader`（slangc 按入口和格式编译，绑定从 SPIR-V 读出；`openmui.shader.rules` 在构建时编译）、`rhi-sdl`（SDL_GPU）、`rhi-cts`（B 级一致性，15 个用例，像素精确）。CI 每一行都有 GPU 并且必须可用：Linux 和 Windows 用 lavapipe，macOS 用 Metal（offscreen 视频驱动做不了 Metal 视图，所以设备用 cocoa 驱动打开，`Options::video_driver`）。
  - ✅ GPU 渲染器（`openmui.render:gpu`）：一个 Slang 着色器画实例化的四边形，只在纹理变化处分批。CPU 精确算的东西，GPU 用同样的方法算：设备对齐的矩形按面积覆盖，渐变查 CPU 的 256 项表，路径、描边、字形、阴影、非矩形裁剪用 raster 的覆盖率进图集，图层是纹理。**和第 7 节 render 一行的差别**：圆角矩形不用 SDF，而是按真实圆弧的面积精确覆盖（旋转后才用有符号距离）；CPU 把圆弧压成 1/32 像素以内的折线，两者最多差 8 级（测试按这个上界检查，其余场景在混合的舍入以内）。GPU 按 CPU 的字节计算（覆盖率、8 位预乘颜色、渐变表、图像整数权重、`mul255`），源覆盖交给混合单元：lavapipe 上矩形、渐变、描边、阴影、裁剪、图层、图像、字形、全部效果与 CPU 逐字节相同。混合模式由第二个着色器（`blend.slang`）在目标的副本上按 CPU 的算法合成；Metal 上 MSL 要求 IEEE 运算、不做收缩（`#pragma METAL fp`）。效果层在自己的表面上离屏绘制（图层加效果的范围），内置的 9 个效果都有 GPU 路径：逐像素效果用效果模板（`effects/lib/openmui_effect.slang`，参数和 CPU 预先算好的值，64 位哈希用两个 32 位字），模糊是整数盒式滤波，像素化是计算着色器，粒子一格一个实例。程序自己的效果：`modules/gfx/render/effect`（`openmui.render.effect`）读 `<名字>.effect.omui.toml` 和 Slang 着色器（基于效果模板，不写 C++），检查后按设备格式编译、注册到渲染器的效果表；文件、着色器或它导入的文件改了就重新加载，原地替换（ref 不变，代数加一，GPU 渲染器下一帧换管线）；只绑定模板提供的资源，否则拒绝；slangc 的报错变成带行列的诊断，坏文件保留旧效果。`openmui.shader` 知道入口时一次 slangc 启动、各格式并行：加载约 170ms、重新加载约 160ms（验收 ③ 要求 500ms 以内，测试每次都量）。宿主的 GPU 呈现：`rhi::Device::present`（rhi 检查，后端 `show`），`rhi::sdl::Options::window` 认领窗口、交换链按显示器同步，帧 blit 进交换链；`openmui.host.sdl3` 的 `renderer`（automatic：有硬件 GPU 用 GPU，只有软件光栅器时用更快的 CPU 渲染器）和 `effects` 目录（随窗口加载，帧之间重新加载）。验收 ③ 端到端：窗口运行时保存着色器，到屏幕上出现新画面 167ms（宿主测试每次都量）。播放器清单的 `effects`（目录）和 `--gpu` / `--cpu`。外部纹理：`GpuOptions::external` 把 id 映射到程序在同一设备上建的纹理（解码的视频帧、引擎视图），按图像画（和 CPU 画同样像素的结果相差不超过 1 级）；零拷贝导入（dmabuf / IOSurface / D3D 共享句柄，`import_external`）随 M5 的硬件视频路径。
  - ✅ 液态玻璃的通用能力（D-64）：背景输入（`EffectInfo::backdrop`，CPU 在绘制到该层时拷贝目标，GPU 把父目标拷进效果的 aux 纹理；`backdrop_blur` 用两边相同的整数盒式模糊磨砂）、形状输入（`EffectShape` 圆角矩形，模板的 `shapes_distance` / `smooth_min`，玻璃组的成员平滑合并）、damage 依赖（带背景输入的效果被任何 damage 触及就整块重画；CPU 一帧一个条带）。内置效果 `liquid_glass`：倒角处按距离场梯度折射、色散、磨砂、自适应着色、边缘光，CPU 参考和 GPU 在 lavapipe 上逐字节相同，CPU 像素固定哈希。组件包 `components/liquid-glass`（`openmui.ui.liquid_glass`，契约优先，设计里写的 `effects/liquid-glass` 按仓库的分层放在 components/ 下）：`liquid_glass` 在自己和子节点外推效果层，形状是自己的圆角盒；`glass_group` 一层画所有成员，形状平滑合并；语义只是普通的分组，有无玻璃、减少透明度时完全相同；减少透明度（`Runtime::set_reduce_transparency`、`DesktopOptions`、`--reduce-transparency`）时画成不透明面板。基准 `glass/10-1080p-cpu`、`glass/10-1080p-gpu`（1080p、10 个玻璃元素）进了第一份基准报告：RTX 4080 上 p95 0.82ms；核显的数字还没有（本机核显未启用）。
  - ✅ 画布核心 `components/canvas`（`openmui.ui.canvas`，契约优先）：按列存储的场景（double 世界坐标、键、种类、填充、圆角、按名的数字和文本列，`push` 一次交付上万个节点）、均匀网格索引（查询的代价随结果而不是场景大小）、double 精度相机（以相机为原点绘制，十亿单位外也清晰）、拖动 / 触控板平移、以光标为中心缩放、双指缩放、甩动惯性、命名视图之间的 spring（对数缩放）；节点种类的档位（实例化圆角框、自定义绘制、晋升为实时组件，聚焦的提前一档；实时组件按世界尺寸布局、相机缩放显示，不重排，同一帧随相机移动）；语义（`OMUI_EXT_canvas`：屏幕上的节点和聚焦节点作为虚拟子项，`find` 搜索整个场景，视口动作 `pan_to` / `zoom_to` / `fit` / `focus_node` / `camera_preset`）。基准：1000 个窗口平移、缩放和 10 万个节点平移，每帧 CPU 约 0.02ms（预算 1ms）。纹理档位：种类的 `content` 在 UI 线程生成显示列表，在共享线程池按屏幕分辨率需要的 mip 级别光栅化，按屏幕面积和离中心距离排优先级；没好之前拉伸最近的已就绪级别，一个都没有时画框；每帧进入图像库的字节有预算（GPU 每张只上传一次），缓存有内存上限（LRU）；内容版本变了（`Scene::touch`）旧纹理留到新的就绪；场景通知画布重画（合并成一次）。GPU 渲染器的图像库图像按 id 只上传一次（`RenderReport::uploaded` 可查）；`FontCollection` 的字体实例在多线程下安全。基准：清晰化约 1ms（预算 300ms）。
  - ✅ 画布行为和 canvas-kit `components/canvas/kit`（`openmui.ui.canvas.kit`）：
    - **行为机制**：画布核心 `world::Behavior` 按名字注册，画布的 `behaviors` 里写 `名字` 或 `名字:参数`。按下事件进手势竞技场，从高优先级起依次询问，接手的行为持有指针直到松开；没人接手的按下用来平移，没人接手的单击作为 tap 再问一遍。行为可以在节点下面和上面绘制，给节点的语义项加状态和动作并执行这些动作；完成的修改作为画布事件发出（`moved`、`resized`、`connected`、`disconnected`、`deleted`、`selection_changed`）；画布放下行为时调用 `unmounted`，行为借此释放图像。
    - **场景**：连线 `Edge`；未注册的种类按名字登记成框；边界跨移动保持（拖动不再遍历整个场景）；新增 `each_rect` 按列遍历。
    - **canvas-kit 提供八个行为**：
      - select：单击、⇧ 加减、背景框选、Esc、Ctrl-A，语义动作 `select` / `deselect`；
      - drag：带上整个选区，可按标题栏拖，Esc 复原，语义动作 `move_to`；
      - resize：八个手柄，⇧ 保持比例，最小 8px，语义动作 `resize`；
      - snap：对齐视野内节点的边和中线，带对齐线，也可以对齐网格；
      - connect：⌥ 拖动连线，语义动作 `connect` / `disconnect`；
      - grid：任意缩放下格子都是 16–32px，细线渐显；
      - minimap：差分数组把整个场景光栅化成一张图，每帧分批处理 12,000 个节点，场景变化时最多每秒重做十次；
      - delete：Delete 键删除选区，语义动作 `delete`（irreversible，回答 yes）。
    - **声明式行为** `*.behavior.omui.toml`：
      - 一个 statechart。事件有 pointer_down / move / up / cancel、tap、key；状态有 entry / exit，执行顺序和 SCXML 相同；可以声明带 when、参数类型和回答的语义动作。
      - 表达式用 `openmui.expr` 在 C++ 里求值，每帧路径不进入脚本。
      - 加载时检查每个键、状态、事件、操作、参数个数、表达式里的函数和名字、参数类型，以及 S-1（irreversible 动作必须声明回答）。每个问题都带行号、列号，并给出最接近的正确写法；文件有任何问题就整份不注册。
    - **行为 CTS**：所有已注册的行为（带参数和不带参数的，以及声明式的）都要经过 120 次种子随机手势、键盘和滚轮输入，再加一次中途取消。要求是：同样的输入跑两次，场景结果一致；每个节点都通过 spec §3.2 检查；声明的每个动作都按声明的参数类型调用一遍；行为被放下后，它创建的图像都被释放。
    - **基准** `canvas-kit/drag-100k`：在 10 万个节点里拖动 10 个选中的节点，同时开启 select、drag、snap、grid、minimap，p95 约 0.26ms（预算 1ms）。
  - ✅ 视图纹理档位和画布修正：
    - 种类的 `view`（返回 Element）在线程池上用离屏 Runtime 布局和绘制成纹理，和晋升后的实时节点逐字节相同；换主题时重画。
    - 确定性时钟（headless、视频导出）下，`Services::deterministic()` 让下一帧等待已发出的后台任务，结果按发出顺序进入。缓存析构时等待正在运行的任务。
    - 双向绑定的回写不再打断 spring。
    - 画布尺寸变化时，实时节点立即重新摆放；画布最小尺寸为 0，不被晋升节点撑大。
    - 新增 `start = "fit" | "preset:<名字>"`：fit 在用户移动相机前，会跟着尺寸变化重新适配。
    - 种类绘制可以拿到 FontCollection。
  - ✅ 示例 10 `examples/10-canvas-basics`（`space`，真实工具：磁盘空间地图）：
    - 文件夹用 squarified treemap 摊在画布上。
    - 只用画布核心和默认交互；档位依次是框、名字标签（只在放得下时绘制）、实时文件卡片。
    - 标题栏显示最大的五个文件，点击即调用 `focus_node` 飞过去。
    - agent 可以用 `find` 按名字查找整棵树。
    - 测试覆盖扫描、treemap 不变量（包含、不重叠、面积比例）、人和 agent 的操作，像素固定。
  - ✅ S1 Markdown 墙 `showcase/markdown-wall`（`omui-wall`，真实工具：文档墙，可以用来读 `.agents/docs`）：
    - 工作区按文件夹划分，窗口在工作区里按行排列。
    - 档位：远处是色块加标题条；中间是窗口自身元素在线程池上离屏渲染的纹理，同时按 mip 级别逐步清晰化；近处或聚焦时变成实时文档。
    - 工作区按标签拖动用声明式行为 `group-drag.behavior.omui.toml`，构建时通过 `openmui.rules` 的 `behaviors()` 嵌入程序，Esc 复原；窗口按标题栏拖动，选择、吸附、小地图都来自 canvas-kit。
    - 视图：1 是 1:1，2 是总览，H 切换 HUD（`views.behavior.omui.toml`）。
    - HUD 用立即模式写：帧时间曲线、各档位的数量、纹理缓存、清晰比例。
    - 流式输出：`--stream N` 或 verb `wall.stream`，每秒 50 个 token。实时窗口立即追加，纹理窗口最多每秒标记过期四次。
    - 通用能力：`Runtime::component<C>()`；声明式行为的 `hit.inside`（框连同上面的东西一起拖）、`preset` / `zoom_to` / `fit('all')`，按键事件在手势期间也能看到 `hit` / `travel`。
    - 测试：7 项，像素固定。
    - ✅ 基准（tests/bench 的 `wall/*`，CPU 预算进门禁）：
      - 1000 个窗口缩放扫描 1%↔400%，p95 0.12ms；
      - 平移 0.012ms；
      - 拖动 100 个窗口的工作区 0.23ms；
      - 1000 个选中窗口一起拖 0.35ms；
      - 20 个窗口流式输出 0.65ms；
      - 清晰化约 50ms（预算 300ms）。
      - 为此做的改动：相机移动时不晋升（聚焦的除外），停下 120ms 后再晋升；小地图任何规模都最多每秒重做 10 次；`Runtime::frame_rate()`，HUD 显示 FPS。
    - ✅ 三种语言的场景脚本 `scripts/tour.{ts,lua,py}`：
      - TS / Lua 在进程内通过 openmui.lang 会话运行；Python 经 agent 端点用 `openmui_agent` 驱动（纯标准库客户端，随 wheel 发布）。
      - 测试要求三者记录的答案和留下的墙（相机、窗口位置、选择）完全相同。
      - 协议新增 `invoke`（调用应用的 verb，S-1 规则同 `act`）。
      - 实例化（晋升）后的节点仍然是它的条目：语义树合并持有者的描述，持有者的动作照常路由。
    - ✅ 帧需求与帧节奏（D-70）的平台无关部分：
      - `FrameDemand`（L2 RAII 租约 `demand_frames`，租约在运行时销毁后自动失效）；
      - L0 自动来源：动画、300ms 内的输入、定时器节奏；
      - 合并规则 `merge` 和 `frame_interval`；`FrameOutput::demand`；trace 计数 `frame.demand_hz`；
      - 按预测上屏时间求值（`FrameInput::present_at`）；
      - 桌面宿主按需求的节奏出帧：低于刷新率时等待，空闲时睡眠，没有 vsync 时也不空转；
      - 画布在相机自行移动时请求最快，S1 流式输出请求 60Hz；HUD 显示请求的刷新率和原因。
    - 还没做：平台映射（macOS ProMotion 的 `CAMetalDisplayLink`、Windows VRR 的 tearing present）、呈现反馈校准、真机实时帧节奏报告。
  - ✅ 风格包与 S2 风格工坊：
    - **新效果**：neon、crt、paper、wobble、metaball 五个效果，CPU 参考实现和 Slang GPU 路径在 lavapipe 上相差不超过一级，像素固定。
    - **`*.style.omui.toml` 风格包**：起始主题、token 修改，以及按窗口 / 组件类型 / role 指定的效果。未知效果或参数会报错，并给出最接近的名字；`write_style` 能写回原文件。
    - **运行时**：
      - 效果规则，节点自己的 `pixel_effect` 优先；
      - 效果层的形状取节点实际绘制的表面，组件可以通过 `effect_shapes` 报告轨道和滑块；
      - 换风格时与上一帧交叉淡化；`apply_style`。
    - **五个风格包**（modules/ui/theme/styles）：glass、neon、retro、ink、liquid。`openmui.rules` 新增通用的 `embed()`。
    - **S2 `showcase/style-gallery`**（`omui-styles`）：
      - 真实的设置面板加仪表盘，可在五种风格之间切换，带交叉淡化；
      - 可以编辑强调色、圆角和各效果参数，即时生效；导出 verb 为 external；
      - 语义树在五种风格下完全相同；测试覆盖切换、编辑、导出、可用性，像素固定。
    - GPU 开销进了第一份基准报告（`docs/{en,zh}/benchmarks/`）：独立显卡上满足 120Hz；核显的数字还没有（本机核显未启用）。
  - ✅ 第一份公开的基准报告（M4 ④，`docs/en/benchmarks/`、`docs/zh/benchmarks/`，数据 `docs/benchmarks/*.bench.omui.json`）：
    - `omui-bench --report / --report-zh`：机器（CPU 型号、内存、系统、GPU）、方法、按组的结果（p50 / p95 / p99 / 最大 / 分配）和预算、每项预算的理由、说明；CI 的 release 行同时保存报告构件。
    - 基准新增：`glass/10-1080p-{cpu,gpu}`、`styles/<风格>-gpu`（S2 五种风格）、`wall/zoom-sweep-1000-gpu`（S1，GPU 每帧绘制）。GPU 基准记下设备；`State::skip` 让没有设备的基准报告为跳过而不是零；预算的 `unless` 让 GPU 的 120Hz 预算不用于软件设备。
    - 为了达标做的 GPU 渲染器改造（结果和 CPU 参考的差别不变，测试覆盖）：
      - 覆盖图集跨帧保留：字形、路径、描边、阴影的掩码按输入的键缓存，只在未命中时光栅化和上传；图集超过 2048 行时先清空重编码，单帧确实需要时才继续增长；
      - 圆角裁剪的掩码按键缓存；图像整张一次上传，存储里的图像借用而不复制；释放的图像纹理按尺寸复用；
      - 圆角矩形描边改为解析的“外圆角矩形减内圆角矩形”（CPU 用 even-odd 填充，GPU 用 `KIND_RRING` 精确面积），修正了 CPU 描边在曲线内侧多覆盖约 0.15 像素的误差；受影响的像素哈希重新固定；
      - `Arena` 的大块分配按最合适的块复用，同样大小的帧不再分配。
    - 结果（i9-13900K + RTX 4080）：S2 五种风格每帧 0.24–0.64ms（p95 ≤ 0.92ms，原来 2.6–4.4ms）；S1 缩放扫描 p99 5.7ms（原来 p95 27.9ms）；玻璃 0.82ms。
  - ✅ 着色器在进程内编译（`openmui.shader`）：slangc 旁边有 Slang 的库时，加载一次，用 slangc 自己的参数编译，会话保留；输出、反射、依赖和诊断与 slangc 相同（测试两种方式对比）。热重载在本机从约 160ms 降到约 50ms（macOS CI 原来 540ms，超过 M4 的 500ms）。
  - ✅ 国际化（D-74，`modules/content/i18n` + `modules/content/i18n/cldr`）：
    - 引擎只依赖标准库和 JSON / TOML 库，不内置任何语言：BCP 47 和 POSIX 标签、似然子标签、语言协商（同一书写系统优先，简繁互相回退）；CLDR 复数规则（按显示的数字选择）；数字、紧凑数字（1.2K、12万、12萬）、日期时间（各长度、骨架、灵活时段“下午”、时区偏移）、相对时间、列表；MessageFormat 2（函数和选项、声明、多选择器）；消息目录、翻译检查、伪本地化。测试结果与 CLDR 一致。
    - `omui-gen cldr` 从 CLDR 49 生成 en、zh、zh-Hant 数据包和似然子标签；`openmui.i18n.cldr` 把它们嵌入程序。
    - 框架接入：`languages()`（数据包和目录、请求的语言、翻译器）、`tr()`（语言或参数变化时原地更新，ID 不变）、视图文档的 `{"tr": …, "args": {…}}`、表达式 `tr(key, {…})`、Lua / TS / Python 的 `tr`（参数按名字）；`--lang`、SDL 的首选语言、`AppSpec` 的目录和源语言。没有翻译的键按 MessageFormat 2 显示它自己。
    - 中文排版（`openmui.text`，按 CSS Text 4 的默认值，`TextStyle::autospace` / `trim_punctuation` 可关）：汉字与西文字母、数字之间自动留 1/8 em（行尾不留）；全角标点按字形墨迹所在的半边分成开、闭、居中三类，相邻时挤掉空的半边（。」、「（ 各 1.5 em），开括号在第一行以后的行首挤掉左半边；行首避免闭标点由 UAX #14 断行保证。分类读字体本身的度量，所以简体和繁体的标点位置都对。
  - ✅ 修复：画布纹理任务按线程缓存字形，原来以字体集合的地址为键；地址被复用时（macOS 上常见）会拿到上一个集合的字形，粗体变常规。字体集合现在有唯一 ID，缓存以 ID 为键，集合销毁后清除。
  - ✅ 规范 0.3（`spec/SPEC.md` §16）：
    - 扩展条款：K-1–K-6 画布（条目、`find`、视口动作、实例化条目、行为、声明式行为）；E-1–E-6 效果和背景输入；ST-1–ST-3 风格包；P-1–P-3 帧节奏；T-1、T-2 纹理档位和确定性；VD-1 视频。
    - 总线 `invoke`；registry 升到 0.3，新增扩展 `OMUI_EXT_backdrop_effect`、`OMUI_EXT_style_pack`、`OMUI_EXT_frame_pacing`；`openmui.h` 等由生成器重新生成。
  - ✅ 开放标准（D-78）：规范拆成 OpenLifa 的两个仓库，各一个 PR、全平台 CI：[openlifa/openmui](https://github.com/openlifa/openmui)（本仓库的 `spec/` 子模块，包 `openlifa.openmui` 0.3.0）和 [openlifa/openas](https://github.com/openlifa/openas)（openas 0.2：actor 文本形式、在场、人的动作、系统上限下的授权和单个请求的决定、退出码 3 和错误码）；两个包都已发布到 mcpp-index（#603、#604）。openmui 的信封字段、actor 写法、未知效果的读法与 openas 一致。
  - ✅ agent 授权（S-11，D-79）：`openmui.core:access` 用 `openas::decide` 决定总线上的每个请求；`AppSpec::agent`（上限）和 `AppSpec::grants`，`agent_access().give` 由人在应用界面里授权；协议测试覆盖上限、授权链和其他 agent。
  - ✅ 窗口（D-75）：
    - `openmui.view:window`：窗口状态（正常、最大化、最小化、全屏、活动、关闭中、自绘、透明）是信号，操作是动词 `window.*`（含切换外框的 `window.frame`）；`window_area_at` 问组件各部分是什么（`ComponentType::window_area`、通用属性 `window_area`）。
    - `openmui.ui.window`：`window_frame`（阴影、圆角边、标题栏、跟随状态）和 `caption_button`（标准和红绿灯两种风格，名称 en / zh-Hans / zh-Hant），契约优先；`FramedWindow` 把外框挂在应用下面（`mount_beneath`、`set_slot_insets`），应用的路径不变；两种风格的像素逐字节固定。
    - SDL3 宿主：无系统外框的透明窗口，`SDL_SetWindowHitTest` 经 `window_area_at`，状态事件，运行时切换，关闭时立即在逻辑上关闭、不再接收输入、输入穿透（Windows 分层窗口、macOS `setIgnoresMouseEvents`、X11 输入形状；Wayland 没有客户端能用的办法），溶解只是呈现。
    - `AppSpec::window`（native / framework / switchable）和 `--window`；headless 也画外框，用于确定的截图。
- ✅ 目录与命名（D-80，[structure-render-web-game](2026-10-28-openmui-structure-render-web-game-design.md) §3）：
  - `modules/` 按层分组：`base`（hanim、geom、memory、unicode、fuzzy、trace、session、pipeline、image、testing）、`gfx`（rhi 和它的 `sdl` / `cts`、shader 和 `rules`、raster、render 和 `effect`）、`content`（text、layout、markdown、i18n 和 `cldr`）、`ui`（openmui：core、view…，主题和原生模块的包在 `theme/`、`native/` 里，宿主在 `host/`）；`lang/` 本身是 `openmui.lang`，三种语言是它里面的包；`tools/cli`。
  - 按角色改名：`openmui.anim` → `hanim`（`hanim::`）、`openmui.slang` → `openmui.shader`（`omui::shader`、`omui-shader`）、`openmui.record` → `openmui.session`（`omui::session`）、`openmui.effect` → `openmui.render.effect`、`openmui.ui.canvas_kit` → `openmui.ui.canvas.kit`。
  - lint：`layering` 按分组（独立组件、openmui、mgame；gongfu 的核心只依赖标准库）、`layout`（路径就是包名、有包的目录都有列出它们的 README）、`thirdparty`（`mcpplibs.mwrap.*` 只在 `native/` 单元里，三处移进了 `native/`）；自测 13/13。
  - 每个分组目录一份 README；主 README 以作品为中心重写（英文为主，附 `README.zh.md`），仓库加上 Apache-2.0 的 `LICENSE`。
  - 发现：`mcpp` 对带引号的三段依赖键（`"openmui.render.effect"`）编译但不链接，改用不带引号的写法（和带引号的 `"openmui.render"` 是不同的键，合法 TOML）；fribidi 是 LGPL-2.1 且静态链接，违反 D-83（下一条）。
- ✅ 许可（D-83）：
  - lint `licenses`：`tools/lint/licenses.toml` 列出工作区经由清单和 `mcpp.lock` 用到的每个 mcpp-index 包和它的许可（SPDX）；没有列出的包、GPL / AGPL、不在 `[dynamic]` 里的 LGPL 都会失败；有本地索引时，许可要和索引的描述一致。自测 14/14。
  - 双向文本从 fribidi 换成 SheenBidi 3.0.0（Apache-2.0，Unicode 17）：上游新增 `compat.sheenbidi`（mcpp-index #607）和 `mcpplibs.mwrap.sheenbidi`（mcpplibs/sheenbidi-m，mcpp-index #608），模块的测试在所有平台上跑 Unicode 的 BidiTest（770,241 例）和 BidiCharacterTest（91,707 例），0 错误。`openmui.text` 按 UBA P1 逐段解析（换行结束一段，下一段按自己的第一个强字符定方向，新测试），L1 的段落部分自己做，行尾部分仍在排版里；固定的像素哈希不变。
  - fribidi 只剩 SDL3 在 X11 上的可选用法：编译时用头文件，运行时 dlopen 系统的 `libfribidi.so.0`（消息框的 RTL 文本），登记在 `[dynamic]`。
- ✅ 统一渲染的第一步（D-81 ①）：
  - `openmui.render` 是渲染架构：`:pool`（瞬时纹理按尺寸、格式、用途从帧到帧复用，120 帧没人要的释放；帧重新编码时 `restart_frame`）和 `:graph`（一帧是 pass 的图：每个 pass 声明读写，瞬时纹理先写后读，否则报出两者的名字；写的东西到不了输出、外部纹理或之后运行的 pass 的 pass 被去掉；其余按描述的顺序记录，每个 pass 一个 trace span，类别 `render`）。
  - 原来的 2D 渲染器成为它的渲染特性 `openmui.render.vector`（`modules/gfx/render/vector`，重新导出核心）；GPU 渲染器的每一帧经由渲染图记录：绘制、混合、效果三种 pass 各自声明读写，`RenderReport::gpu_passes` / `gpu_passes_culled`；纹理池换成核心的；CPU 参考和 GPU 的像素比较全部不变。
  - rhi 补上渲染特性需要的（D-81 ②）：深度和模板目标（`d32_float`、`d32_float_s8`，`DepthTarget` 的清除、加载、保存），多重采样（`TextureDesc::samples` 到 `Caps::max_samples`，在 pass 结束时解析进 `ColorTarget::resolve`），每个管线一个深度测试（`DepthState`）；校验要求 pass 和它设置的管线采样数、深度格式一致；SDL_GPU 后端逐项映射；rhi.cts 加三例（`depth_test`、`depth_kept`、`multisample`，4× 的标准采样位置在三种 API 上相同），lavapipe 上逐像素通过。SDL_GPU 没有 GPU 时间戳查询，记为上游缺口 #3，渲染图暂时只在 CPU 端计时。
  - 测试：`modules/gfx/render/tests/graph_test.cpp`（池的复用和释放、问题的措辞、计划、span，以及真实设备上没被读的 pass 不运行的字节）；vector 的 GPU 测试加一条：一帧的每个 pass 一个 span，同一帧再画一次 pass 相同。
- ✅ Web 的第一步（D-84，W1 ①）：
  - 工作区对 `wasm32-emscripten` 的设置：异常（JSON、TOML 和捕获它们的 native 单元需要）、可增长的内存、1 MiB 的栈；测试程序的配置 `wasm-test`（`-sNODERAWFS=1`：在 node 下读源码旁边的数据，和其他行一样）。
  - 包用 mcpp 的 `[package] platforms` 声明自己能在 Web 上运行；lint `platforms`：这样的包只依赖同样声明了的包（开发依赖也算，测试也在那里跑），自测 15/15。
  - 现在声明的 14 个：hanim、geom、memory、unicode、fuzzy、trace、pipeline、image、testing、raster、layout、i18n 和 cldr、markdown。CI 新增一行 `test web wasm32-emscripten`（`tools/ci/web.sh` 按清单选包，在 node 下跑测试）：21 个测试程序全部通过，固定的哈希——hanim 的数学位、raster 的像素、CommonMark 652/652——和桌面各行是同一组常量。
  - 目标能做什么用分区常量表达，不用宏：`openmui.testing:target` 按目标选源文件（`threads`：Web 行没有线程）。
  - 依赖 `mcpplibs.os` 的包（text、session、ui/*）等上游：mcpplibs/os#8 和 openkal-emscripten#8 已经就绪（CI 绿），os#8 建在另一项工作的 os#7 上；mcpp 的三个 Emscripten 缺口报在 mcpp#851。
- ✅ gongfu 的核心（D-85，第一部分；`mgame/gongfu`，包 `gongfu`，只依赖标准库，也在 Web 行上测试）：
  - `:fixed` Q16.16 定点数（乘积向下取整、商向零截断，字面量在编译期转换）；`:input` 64 帧输入记录（相对朝向的方向、按住和按下、SOCD 三种）、指令（窗口内的搓招、蓄力、按下时的方向、同时按键、缓冲）；`:data` 角色、招式（帧、判定、伤害、削血、硬直、打击停顿、推开、上中下段、击倒、浮空、取消、无敌、位移、投技、飞行道具、能量、超必杀的冻结）和规则（连段补正、反击、回合、场地、近身防御、SOCD），`compile` 逐项检查并带位置；`:state` 一整块没有填充的纯数据（编译期检查），哈希就是它的字节；`:match` 两名角色从帧开始时同时决定、移动、推开、留在场上，所有攻击先判定后结算（同帧相抵），停顿期间按下的取消在停顿结束时开始。
  - 测试 14 项：帧数据实测与声明一致（空挥的发生、持续、收招；命中和防御的帧优势 = 硬直 − 攻击方剩下的帧），防御的上中下段，取消连段和补正，反击，投技和拆投，飞行道具和相抵，无敌升龙；随机 10,000 帧上 10,000 次随机深度的回滚，每一帧的状态和不回滚时逐字节相同，哈希在 GCC、LLVM、wasm 上相同并固定。
  - `gongfu.data`（`mgame/gongfu/data`）：角色和规则是文件——`*.fighter.gongfu.toml`、`*.system.gongfu.toml`，将来 mod 的格式。招式输入用数字键记法（`236P`、`623K`、`63214HP`、`[4]6P`、`[4:60]6HP`、`2LK`、`LP+LK`，读写互逆）。格式的问题（未知的键带最接近的正确写法、类型、范围）和编译器的问题（帧超出招式、取消进不存在的招式）一次报完，每个都带行号和列号；TOML 本身的语法错误也一样。测试：训练角色的文件和它的 C++ 定义打出同样的连段；Web 行上也通过。
  - `mgame.net`（`mgame/net`，只依赖标准库）：两个对等端的回滚网络，和传输无关：输入延迟、预测（对方最后已知的输入）、回滚（恢复那一帧保存的状态再向前播放）、等待（超过 `max_rollback` 不再前进）、消息带未确认的全部本地输入（丢包随下一条重发）和确认、对方的帧（时间同步）、每 30 个确认帧交换一次校验和（不同步时报出分歧的帧）；消息是小端字节，任何传输都能带。测试在模拟链路上（完美；延迟在输入延迟以内；4、6±3、8±4 帧，0、5%、15% 丢包；落后 20 帧）用 gongfu 的对局对打：两端和离线回放逐字节相同；延迟被输入延迟覆盖时没有回滚，否则有；一端的游戏被改动时报出分歧的帧。GCC、LLVM、wasm 上都通过。
  - `gongfu.present2d`（`mgame/gongfu/present2d`）：对局的 2D 画面，画进 openmui 的显示列表（CPU 参考或 GPU 渲染器）。外观是骨骼（关节、长度、静止角）加矢量部件（胶囊、椭圆、多边形、背后的描边）和按帧打关键帧的片段（hanim 的缓动）；播放的片段由角色在做什么决定（招式名，否则模式），招式按招式帧、硬直按已过的帧、其余按对局帧循环；朝左是朝右的镜像。镜头让两人都在画面里；效果来自按帧保存的事件：命中的光线（反击是红色）、防御和投技的圆环、KO 的闪白、重击的震动。三角函数用 hanim 的确定性函数：三帧的显示列表和 CPU 像素在 GCC 和 LLVM 上相同并固定；回滚后重画同一帧逐字节相同。核心的事件位置改为攻击框和受击框重叠区域的中心（火花落在打中的地方）。
  - 格斗游戏的骨架（`showcase/fighting`，`omui-fight`）：舞台组件（契约 `stage.contract.omui.toml`）在运行时的时钟上以 60 帧/秒推进对局（定时器补帧，最多补 4 帧；超过就丢掉，不重放；headless 下 1 秒恰好 60 帧），两名玩家用键盘，agent 用动作（`press` 用数字键记法持续按住一个手柄状态，`release` 交还键盘；`pause`/`step` 逐帧）；语义值就是对局（帧、剩余秒数、每个角色的名字、血量、连击数、位置、正在做什么）。角色是程序内嵌的 `*.fighter.gongfu.toml`，读不了的文件带行列号出现在语义值里。训练模式按画面的镜头（含震动，present2d 新增 `view_camera`/`on_picture`）画出推挤、受击和生效中的攻击框，以及 60 帧的帧表；血条从中间向各自一侧减少，计时器是七段数码，不需要字体。测试：60 帧/秒、键盘、暂停和逐帧、agent 走近出拳命中，对战和训练画面的像素固定。
  - 格斗游戏的性能预算（D-85 §8.6）：`tests/bench` 新增 `gongfu/frame`（单帧模拟，实测约 1 µs，预算 p95 ≤ 20 µs）、`gongfu/rollback-8`（回滚 8 帧并重算，每帧保存状态，实测约 90 ns/帧，预算 ≤ 2 µs/帧）、`gongfu/present2d`（一帧画面的显示列表，实测约 30 µs，预算 p95 ≤ 0.25 ms）；三项都是零分配，并写进预算。present2d 新增 `Painter`，在帧之间复用姿势、部件顺序和路径；`DisplayList::reserve` 让每帧重建的列表在大小变化时也不分配；舞台改用它们。
  - 外观成为文件，打击感成为数据（D-85 §8.3/§8.4）：`*.look.gongfu.toml`（`read_look` / `write_look`）包含调色板（命名的槽，左右两侧各一套）、骨骼（角度用度）、部件、片段（按帧的关键帧，片段的缓动加上个别关键帧自己的缓动）和冲击配置；所有问题一次报出，带行列号和最接近的正确写法；写出的文件能读回成同一个外观。冲击配置包括震屏、镜头推近、闪白或负片、火花（`rays`、`ring`、`splash` 墨点、`shards` 碎片、`bolt` 电光，以及大小、数量、帧数、颜色、反击色）、声音名和震动。角色文件的 `impact` 改为按名字引用（`FighterDef::impacts`），防御、投技、KO 用外观里的 `block`、`throw`、`ko`，没有配置的用默认值；火花的散布是事件的哈希，回滚后画出来相同。Kata 的外观从 C++ 搬到 `fighters/kata.look.gongfu.toml`，`gongfu.data` 导出 `nearest_hint` 给其他格式复用。
  - 训练模式（D-85 §8.5）：新包 `gongfu.training`（只依赖标准库，每帧零分配，也在 wasm 上测试）在每帧之后从对局读出：输入历史（每个手柄状态一行，带按住的帧数）、帧表（发生、持续、收招、受击硬直、防御硬直、倒地、被投，不计打击停顿和超必杀的冻结帧，所以一个招式的长度就是它的帧数据）、两边都恢复后的帧优势、连段的段数和伤害，以及按玩家录制并循环回放的假人 `Recorder`；测试按数据核对（拳的 3/2/7，拳和重拳在命中和被防时的帧优势，拳取消波动拳是 2 段 900 伤害）。格斗游戏的训练模式改用它：判定框加上投技范围和飞行道具，80 帧的帧表，两侧的输入历史（箭头、按键颜色、帧数），计时器下方的发生、帧优势、连段读数（数字用七段，几个词只排版一次并缓存）；舞台新增动作 `save`、`load`、`record`、`replay`、`stop`；语义值的 `training` 给 agent 同样的读数。HUD 拆成 `:hud` 分区。
  - 格斗系统 0.2（[角色设计](2026-10-28-gongfu-characters-design.md) G-1..G-10，经 codex 评审后定稿的部分）：冲刺（只有方向的指令 `66` / `44`）；飞行道具的速度向量（可以下落、落地消失）、多段和间隔、强度（相撞时强的留下）、同屏数量限制；后投（交换位置）、拆投窗口从投住起算、起身 3 帧和硬直结束 2 帧不可投、起跳前可投；无敌按种类（打击、空中、飞行道具、投技）；招式自己的逐帧受击框；追打点数和空中复位（每次浮空最多再浮空 1 次）；打击停顿按方计算（飞行道具的主人不停），超必杀冻结期间一切暂停、输入缓冲到冻结后第一帧；被打方得到伤害的 1/10 能量；击倒招式自己的倒地帧数；同优先级时指令更具体的先读（2HP 不会被读成 5HP）。`*.fighter.gongfu.toml` 增加对应的键（`hitstop = { attacker, defender }`、`down`、`juggle`、`juggle_cost`、`invulnerable` 的 `to`、`[[move.hurt]]`、飞行道具的 `velocity` / `hits` / `interval` / `strength` / `limit`、投技的 `swap`、记法 `66` / `44` / `4LP+LK`）。新的 `tests/rules_test.cpp` 逐条测试，再用 5000 帧覆盖全部规则、逐帧回滚，和不回滚的结果逐字节相同，哈希固定（GCC、LLVM、wasm 相同）；原有测试的回放哈希重新固定。
  - 从动画烘焙受击框（G-5，角色设计 §6）：外观的骨骼可以标 `limb = true`；present2d 的 `reach` 算出某个姿势下哪个肢体伸出了站姿的受击框（和站姿的静止姿势相比，超出 12 像素才算）；新包 `gongfu.studio`（库 + 命令行 `gongfu-studio`，JSON 输出和退出码）的 `bake` 对每个招式的每一帧摆姿势，把伸出的肢体框按 5 像素网格取整、相同的相邻帧合并，写成 `<名字>.boxes.gongfu.toml`（带源文件的哈希），`check` 检查是否过期，`embed` 把一个目录的角色（角色、外观、受击框）生成模块分区；新包 `gongfu.rules` 是构建插件，在构建里运行 `embed`，受击框过期时构建失败并给出重新烘焙的命令。`gongfu.data` 读写 boxes 文件，`read_fighter` 可以带上它；模拟的 `Match::hurtboxes` 公开，训练模式画的就是模拟用的受击框（出拳时手臂的框可见）。present2d 测试新增 Kata 全部片段的姿势总览图（三个时刻），借它修正了 Kata 的蹲、下段踢和跳跃姿势；openmui.rules 改用限定名 `openmui.rules` 找自己的目录（和 `gongfu.rules` 的短名相同）。
  - 墨（第一个设计角色，[角色设计](2026-10-28-gongfu-characters-design.md) §3）：角色文件的数值由设计表算出（32 个招式：普通技、跳攻击、三种速度的「字」、空中「点」、只对空无敌的「挑」、可被投的「墨遁」、超必杀「一字千钧」、冲刺、前后投）；外观第一版（纸白长袍、宽袖、发髻、作为道具的笔，41 个片段，墨点冲击）；格斗游戏的测试在模拟里实测每个普通技、「字」和「挑」的发生帧和命中 / 被防的帧优势，和设计表一致（飞行道具的停顿只算被击方，这一点由测试发现并写进设计记录）。present2d 新增 `Painter::body`（单个角色的画法）、骨骼的 `prop`（拿着的道具不算受击框）、片段按力度共用（`236P` 用于 236LP/MP/HP）；`gongfu-studio sheet` 画出外观的全部片段（持续帧实心、首末帧淡色、攻击框和受击框），用来核对动作和判定；舞台新增 `left` / `right` 属性选择角色。
  - 格斗系统 0.3 的核心规则（[架势与超必杀设计](2026-10-28-gongfu-posture-supers-design.md) P-1..P-4，经 codex 评审后定稿）：架势（防御按整招积累、破势踉跄、追击清零 + 势回 + 80% 补正、按姿态不同速度恢复）、拼刀（攻击框相交：超必杀 > 时机相同时比分量 / 分量也相同为完美拼刀双方不变 > 时机不同时新鲜的赢；无敌时不拼）、完美格挡（读屏幕方向的新按下，改善 4 帧，不掉架势和削血，可以逐段弹）、3 气能量和超必杀的补正锁定、超必杀命中不回气；数据文件和规则文件的对应键；逐条测试，全规则逐帧回滚覆盖拼刀 / 弹 / 破势，哈希三平台相同；格斗游戏加上架势条和三格能量，墨的数值按设计更新。
  - 三气的锁定和超级动画（P-5、P-6 第一部分）：模拟里三气第一击命中后双方锁定，按帧结算动画里的每一段伤害、计时器暂停、结束时按数据摆放（锁定中的 KO 等到结束）；画面层新增超必杀冻结时的镜头推近、压暗、角色色调光带和三气的放大特写，外观文件的笔画字形和超级动画时间线（片段、背景、镜头、逐帧写出的字、印章、闪光）；墨的三气「永字八法」在宣纸上按笔顺写出「永」的八笔，每笔一段伤害并对齐模拟的帧，最后盖朱印——格斗游戏的测试固定了冻结特写和 6 个动画画面。舞台新增训练动作 `fill`（满能量、清架势）和 `player1` / `player2` 属性（原来叫 left / right，和布局的样式属性同名而被吞掉——这是框架该在生成器里报错的地方，记为待做）。
  - 霓（第二个设计角色）和画面的新能力：角色文件按设计算出，实测发生和帧优势和表一致；招式的 `pass` 帧不推开对手（二气穿过对手）；present2d 新增拖影（`trail`，由 Effects 按帧保存的角色状态画出，回滚后相同）、霓虹光晕（`glow`）、超级动画的扫描线；飞行道具可以画成字（G-11，墨的「一」「人」「永」在笔下写出后飞出，带纸色光晕）；墨的三档超必杀补齐。运行时修了一处框架缺陷：合约里和布局属性同名的属性（文字的 `align`、舞台的 `left`）以前只交给布局、组件收不到，现在两者都收到（全工作区 83 个成员的测试通过）。
  - 岳（[角色设计 2](2026-10-28-gongfu-characters-2-design.md) §3，第三个设计角色）和他需要的规则 G-16..G-18：霸体（招式的帧吸收若干段打击，伤害照收、双方停顿；投和超必杀不受影响）、转圈指令（`360` / `720`，按经过的四侧计圈，可以从起跳前发动）、空中投（只抓空中的对手，抓住后拉到地面）；三气的投技抓住即进入超级动画。核心修了一处缺陷：超必杀的冻结以前从招式第 1 帧之后才开始，第 1 帧生效的投会在闪光的同一帧抓住、冻结被推到动画之后；现在请求超必杀的那一帧就进入闪光，招式第 1 帧在冻结之后（设计写的“冻结后第 1 帧生效”，有专门的测试），墨、霓的画面哈希因此整体后移一帧并重新固定。present2d 新增超级动画的路径（从锁定原点算起的举起、跃起、砸下）、画面震动和冲击环、背景里的云（放在世界里，镜头升起时从旁经过）、地面开裂的火花（`cracks`）、巨汉的脚步让镜头下沉（`footfall`）。角色文件 36 个招式、外观 44 个片段；测试实测普通技和「顶肘」的发生和帧优势，三气「泰山压顶」从防御硬直里转两圈发动，七个画面固定（GCC、LLVM 相同）。
  - 璃（[角色设计 2](2026-10-28-gongfu-characters-2-design.md) §2，第四个设计角色）和 G-15 设置物：状态里每人 3 面镜子，招式可以立镜、碎镜、瞬移到镜子；镜子挡下对手的飞行道具（每段扣 1 点耐久）、被对手的打击扣耐久（一击一次）、被主人的打击打碎成射向对手的碎片；一气的镜子把飞行道具反射回去（飞行道具记住定义它的人，反射后不占额度、不给能量）；规则逐条测试并加进全规则回滚。实现时修了两处核心缺陷：离地的地面招式落下越过地面时会提前结束（现在按招式的姿态判断，有测试）；画面层按状态找片段的表少一项、越界读（Nightly 的 ASan 发现；表长由 `mode_count` 决定，编译期检查）。记法里相邻的相同方向是点两次（`22K` 是下、回中、下）。画面层画镜子（玻璃、高光、耐久低时的裂纹、能反射的金边）和三种新冲击，超级动画新增棱镜像、按命中段数增加的裂纹和画面碎落；「琉璃界」七个画面、镜子在对局中四个画面固定。
  - 刻（[角色设计 2](2026-10-28-gongfu-characters-2-design.md) §4，第五个设计角色）和 G-19..G-22：会折返的飞行道具（朝主人当前位置飞回、去回各中一次）；每人 128 帧的历史（手柄和位置）与刻印、回刻；分身作为第三个身体（决定、推进、受击框按身体编号，读本方 120 帧前的手柄，用本方的招式表，不能投、不用能量、不放超必杀，被对方碰到、本体被打中或时间到就消失）；停摆（额外受击硬直，每连段一次）。规则逐条测试并进全规则回滚（三平台哈希相同，回滚 8 帧约 196 ns/帧）。画面层新增旋转的投射物、分身的色调、回刻的残影、超级动画的时间停止（画面变灰、火花冻住、钟面），渲染层新增 `DisplayList::recolor`。训练模式的判定框按飞行道具的定义者查招式（反射后在 LLVM 下曾段错误）。五个设计角色全部完成。
  - 平衡第 1 轮（[角色设计 2](2026-10-28-gongfu-characters-2-design.md) §9.5）：新包 `gongfu.ai`（从数据读出招式用途；机器人带 14 帧反应、带种子、结果确定；`versus` 逐对比赛并交换左右），`gongfu-studio versus` 输出 JSON 报告（胜率、伤害来源、回合时长）。按报告调了墨的 5HP 判定、霓和岳的血量，六人胜率在 44%–58%；格斗游戏测试里每次跑平衡门槛（每对 100 局，每人 38%–62%，每对 25%–75%，结果哈希固定），P-8 检查扩到六人并新增“连续不能行动不超过 600 帧”。
  - 音频（D-86，R8 的第一部分）：独立组件 `modules/media/audio`（`openmui.audio`）。声音是数据（`*.sound.omui.toml`：正弦表、带限方波和锯齿波、噪声，音高和滤波截止频率按指数滑动，包络、过载、声像），声部按样本精确开始、满了让最旧的，总线带效果插件（滤波、回声）进母线限幅器，按块渲染且不分配内存，播放和渲染之间是无锁队列；DSP 只用 hanim 的数学函数、不收缩不快速数学，离线渲染在 GCC、LLVM、wasm 上逐位相同（测试固定哈希）。基准 `audio/mix-48`（48 个声部一块 128 帧约 0.045 ms，预算 0.5 ms，零分配）。插件 `openmui.audio.device`（miniaudio，经 `mcpplibs.mwrap.miniaudio`；空后端用于无头和测试）。格斗游戏接入：声音库 `sounds/fight.sound.omui.toml`（共用的击打声和每个角色自己的墨、霓虹、玻璃、山、钟声），外观的冲击配置写明三层声音，舞台按模拟的帧播放，桌面版经设备发声；测试把一段对局的声音离线渲染并固定哈希（GCC、LLVM 相同），输出 WAV。规范补充：合约属性和运行时属性同名时两边都收到（规范 §11）。
  - mgame 核心（[S4 设计](2026-10-29-mgame-s4-breakout-design.md)，R8 的第二部分，经 codex 评审）：`mgame.world`（自写列存储：定长列、下标 + 代数、位图，按下标遍历，整块拷贝即快照，校验和是字节的哈希；Q-25 定为自写）、`mgame.sim`（定步长时钟：追帧上限、时间倍率、插值比例；`Timeline`：快照环 + 输入环，`seek` 精确到达保留范围内的任意帧，从那里继续就是新分支）、`mgame.input`（键、手柄、指针、鼠标 → 动作，每帧采样成纯数据，帧内的轻点不丢，录制是可读的文本）、`mgame.physics`（圆对盒的扫掠：四个面外推半径、四个角是圆弧，只认朝里的接触，角的根用不抵消的形式；双精度只用精确舍入的运算，三平台逐位相同）、`mgame.ui`（在运行时的时钟上按步推进游戏；openmui 的键和指针给 `mgame.input`）。S4 的模拟 `showcase/widget-breakout/sim`：挡板按相对运动碰撞、同一时刻的碰撞按下标排定并合并法线（砖缝按面弹）、每帧最多 8 次碰撞、砖块耐久和连击、胶囊（变宽、多球、慢动作、穿透）、生命；事件存在状态里（倒流不重放）；倒流 5 秒再重放逐字节相同。依赖升级：`mcpplibs.os` 0.1.2 → 0.2.2（含 Emscripten；上游 openkal-emscripten 0.6.0、os 0.2.2 由子代理发布进 mcpp-index），本地用和 CI 相同的 mcpp 2026.10.27.1 全量构建和测试。
  - S4 第 1 关（[S4 设计](2026-10-29-mgame-s4-breakout-design.md) §6 第 3 步）：`showcase/widget-breakout` 的界面包 `openmui.showcase.widget_breakout`。场地组件 `field`（契约先行：`level`、`paused`、`lens`；事件 `brick_broke`、`level_over`；动作 `launch`、`move_to`、`release`、`pause`、`resume`、`step`、`restart`、`rewind`）的子节点就是关卡的砖块——真实的按钮、开关、滑块、卡片，由 openmui 的布局排成行；第一步按布局矩形读进模拟（换算到 1280 × 720），之后每次击中都真的改变组件（开关切换、滑块减三分之一、碎掉的砖从画面消失并在语义树里变成 hidden）。`mgame.ui` 的步进器在运行时的时钟上每秒 120 步（无头下精确）；指针、方向键、空格和鼠标经 `mgame.input` 采样，agent 用同一组动作玩。Tab 打开语义透视：每块砖的 role 和 ID、两块隐形砖（画面上透明，语义树里是按钮；看过之后保持淡色轮廓）、语义树最近的变化（也在语义值 `changes` 里给 agent 读）。倒流按模拟状态恢复每块砖。无头测试：砖块和模拟的盒子逐位一致、每秒 120 步、agent 打砖时组件和模拟始终一致、倒流 600 帧校验和与当时相同、超过 30 秒拒绝、语义透视；两帧像素在 GCC 和 LLVM 上固定。`omui-breakout` 是可执行程序。S4 的模拟改为导出 `mgame.sim`（它的 `Game` 就是时间线的游戏）。
  - S4 第 2 关和关卡流程：场地按关卡自己组成砖块（`compose()`，换关就重新组成、重新布局、重新读进模拟），通关显示这一关的一课，Enter 或 `next` 进下一关（没球了重来）。第 2 关“风格”：每打通一行换一个风格包（glass、neon、retro、ink、liquid，交叉淡入），S 或 `next_style` 随时换；场地只发 `style` 事件，应用用 `theme::apply_style` 应用；背景、砖底、挡板、球和 HUD 都用当前风格的颜色。测试：同一个 agent 打两局，一局中途换三次风格，每帧校验和相同、砖块的语义节点相同、像素不同（两种风格的像素固定）；第 1 关打通后 Enter 进入第 2 关。
  - S4 第 4 关“时间”：模拟加入门（`Kind::gate`：在状态里的帧窗口内不参与碰撞，不算要打的砖；整局校验和重新固定，GCC、LLVM、wasm 相同）；金库一行只能经门进入，门是真实的进度条组件（随帧填满、打开时消失并显示倒计时、之后永远关闭），按住 R 每步倒退两帧（最多 30 秒），松开从那里继续；慢动作胶囊让游戏时钟以半速走。关卡按编号查找，`next` 走到下一个已经做出的关（第 3 关以后补上）。测试：门在一半时间时进度一半、窗口内打开、之后关闭；按住 R 一秒退回约 240 帧并回到门开着的时刻，组件和模拟一致；像素固定。
  - 发现并修正：GCC 16 在导入时对默认的友元比较运算符内部崩溃（改成显式比较）；测试里两个实参的求值顺序未指定，曾让 GCC 和 Clang 生成不同的输入（拆成两条语句）。
- M5：openmui-video、ui-timeline、MCP 服务器、skills、S3 / S4 / S5。
- 契约⑦（插件与管线）写进规范；Inspector 的火焰图和动画检查器视图。

## 来源

- GCC 16 的 C++26 反射：[GCC 16.1 released（isocpp.org）](https://isocpp.org/blog/2026/04/gcc-16.1)、[GCC C++ Dialect Options（-freflection）](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)、[cppreference：C++26 编译器支持](https://en.cppreference.com/cpp/compiler_support/26)、[GCC 16.1 发布（Phoronix）](https://www.phoronix.com/news/GCC-16.1-Released)
- 视频工具的性能数据：[Remotion Lambda 成本和耗时示例](https://www.remotion.dev/docs/lambda/cost-example)、[Remotion Render Times（rendercomp）](https://rendercomp.com/blog/remotion-render-time-benchmarks/)、[Remotion Alternatives（Orshot）](https://orshot.com/blog/remotion-alternatives)、[IntelligentMixVideo#251 的实测](https://github.com/HsiangNianian/IntelligentMixVideo/pull/251)、[Remotion Lambda 指南（rendercomp）](https://rendercomp.com/blog/remotion-lambda-cloud-rendering-guide/)、[Revideo：Slow Rendering](https://docs.re.video/common-issues/slow-rendering/)、[yt3#166：Motion Canvas 和 Remotion 的对比](https://github.com/KAFKA2306/yt3/pull/166)、[HyperFrames vs Remotion（ArceApps）](https://arceapps.com/blog/hyperframes-vs-remotion-2026/)
- Remotion 和 agent skills：[Remotion Agent Skills Guide 2026](https://aividpipeline.com/blog/remotion-agent-skills-guide-2026)、[Remotion Skills 2026（gaga.art）](https://gaga.art/blog/remotion-skills/)、[What Is the Remotion Skill?](https://www.ngram.com/blog/remotion-skills-sh-ai-video-creation)
- Opus 5.5 带起的代码动画视频：[The Emergence of Code-Based Animation in Claude Opus 5.5](https://bretkerr.substack.com/p/the-emergence-of-code-based-animation)、[officechai：10 Best Examples](https://officechai.com/ai/claude-opus-5-5-motion-graphic-videos/)、[How to Make an Opus 5.5 Cartoon in Claude Code](https://explainx.ai/blog/how-opus-5-5-viral-videos-are-made-code-not-diffusion-2026)、[Claude Motion 教程](https://help.apiyi.com/en/claude-motion-opus-5-5-prompt-to-motion-video-tutorial-en.html)、[revid.ai：64 Videos + Prompts](https://www.revid.ai/claude-motion-graphics)
- SDL_GPU 的 WebGPU 后端（实验性的 PR）：[libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020)、[libsdl-org/SDL#12046](https://github.com/libsdl-org/SDL/pull/12046)
- 本地：`mcpplibs/os`（分支 `os-0.1.0`）的 README 和 `mcpp.toml`；`mcpp/docs/05-dependencies.md`（git 依赖）、`07-workspace.md`（带根包的 workspace）、`04-mcpp-toml.md`；xim-pkgindex 和 mcpp-index 的包列表
