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
| 动画 | **通用动画引擎 `openmui.anim`**：独立的 workspace 成员，只依赖标准库，将来可以独立成仓库；**时间的纯函数**（可以任意跳转、结果确定，spring 有解析解）；UI、视频、游戏、画布共用；**提前到 M2**（§6） |
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
│   ├── anim/                          # ★ openmui.anim：**通用动画引擎**（只依赖标准库；UI、视频、游戏、画布共用，§6.0）
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
│   ├── record/                        # ★ openmui.record：录制和回放：带时间戳的事件 / 动作流（NDJSON），可以跳转、可以转成测试
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
| **modules/anim** | `openmui.anim` | ✅ **通用动画引擎** | 只依赖标准库，不知道 UI 的存在；游戏引擎、其他 UI 框架都可以使用 |
| modules/unicode | `openmui.unicode` | ✅ 通用库 | mcpp-index 里没有断行库，所以自己做；也可以贡献给 mcpplibs |
| modules/text | `openmui.text` | ✅ 通用库 | C++ 模块化的文本引擎在 C++ 生态里很少见 |
| modules/raster | `openmui.raster` | ✅ 通用库 | 确定性的 2D 光栅器 |
| modules/rhi + 各后端 | `openmui-rhi*` | ✅ | UI 尺度的 GPU 抽象层 |
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
  "runtime/*", "hosts/*", "components/*", "themes/*",
  "video", "lang/*", "tools/*", "examples/*", "showcase/*", "tests/*",
]

[dependencies]
openmui.core     = { path = "runtime/core" }
openmui.view     = { path = "runtime/view" }
openmui.ui.base  = { path = "components/base" }
openmui-theme-default = { path = "themes/default" }

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
| core | 树、ID（S-8 / S-9）、signal、verb、事件、焦点和导航、arena | SoA + 带代数的句柄；`consteval` 校验 ID 字面量；signal 批量提交；**时钟使用 `omui::anim::Clock`，可以注入**（交互、虚拟、渲染三种） | M1 |
| 任务调度（core 内） | 基于 mcpp-index 的 **taskflow**（模块 `tf`）：帧内的并行 for、任务图、优先级（帧关键 / 后台）、取消；UI 线程和脚本线程之间的队列用 `compat.concurrentqueue` | 不自己写线程池（D-56）；热路径上不分配内存 | M1 |
| layout | flex、grid、栈、滚动、虚拟化 | 增量布局、测量结果缓存、**取整规则**（A 级）、`-ffp-contract=off` | M1–M2 |
| text | HarfBuzz 排版、FreeType 字形、fribidi、**UAX#14 断行**、字体回退、选区和编辑、输入法 | 断行表由 `tools/gen` 从 Unicode 数据生成（不额外依赖 ICU）；排版结果缓存；msdf 图集交给 render | M1–M2 |
| anim | 独立组件 `modules/anim/`，见 §6 | 只依赖标准库 | M2 |
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

### 6.0 定位：通用动画引擎 `openmui.anim`（D-49）

- **独立组件 `modules/anim/`**，有自己的 `mcpp.toml`，**只依赖标准库**，不知道节点、视图、渲染的存在。将来可以直接独立成仓库（甚至贡献给 mcpplibs）。
- **使用方**：`openmui.view` 把它适配到节点属性上；`openmui-video` 用它做合成和时间线；S4 游戏用它做玩法里的补间和时间倒流；画布用它驱动相机。**其他项目也可以单独使用**，比如游戏引擎、别的 UI 框架。
- **对外的核心抽象**：

```cpp
export module openmui.anim;
import std;

export namespace omui::anim {

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
auto tl = omui::anim::timeline()
    .at(0.0s, "title.opacity", omui::anim::tween(0, 1, 400ms, omui::ease::out_cubic))
    .at(0.2s, "title.y",       omui::anim::spring(40, 0, {.stiffness = 120, .damping = 14}))
    .stagger(0.05s, "cards/*.scale", omui::anim::tween(0.9, 1, 300ms));
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
| themes/styles | 风格包：token + Slang 效果 + 动效 token | M4（S2） |

每个组件都必须通过组件完成标准（component-binding §12）。

---

## 10. 语言绑定实施

| 语言 | 方案 | 里程碑 |
|---|---|---|
| C ABI | `openmui.h` + `runtime/abi`，由生成器产出 | M3 |
| TS / JS | `compat.quickjs-ng`；TS 在构建期用 esbuild 转换（xim 中已有）；生成 `.d.ts`；可选 TSX | M3 |
| Python | **用 CPython 的稳定 ABI（limited API / abi3）自己写绑定模块**，不用 pybind11（它是依赖宏的头文件库，不符合“不用宏、头文件最少”的规则）；先做“作为宿主”的模式（`pip install openmui`，每个平台一个 wheel）；生成 `.pyi` | M3 |
| Lua | `compat.lua`（5.4）；生成 LuaLS 注解 | M3 |

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
- **风格包** = token + Slang 效果 + 动效 token，放在 `themes/styles/`。
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
- **技术**：游戏循环和 HUD 用立即模式，菜单和关卡选择用保留模式；物理按固定步长计算、由种子决定；时间倒流靠 `openmui.anim` 的分段解析解和输入记录；目标 120Hz。
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
| mcpp-index | `compat.sdl3`、`compat.harfbuzz`、`compat.freetype`、`compat.fribidi`、`compat.msdfgen`、`compat.md4c`、`compat.quickjs-ng`、`compat.lua`、`compat.ffmpeg` / `ffmpeg.ffmpeg`、`compat.miniaudio`、`compat.spirv-reflect`、`compat.libpng`、`compat.zlib`、**`nlohmann.json`**（JSON，模块 `nlohmann.json`）、**`marzer.tomlplusplus`**（TOML，模块 `tomlplusplus`）、**`taskflow.taskflow`**（任务系统，模块 `tf`）、`compat.concurrentqueue`（无锁队列）、`compat.xxhash`、`compat.mimalloc`、`compat.wamr` |
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
| **M4 GPU + 画布 + 风格化** | 8–10 周 | omui.rhi + rhi-sdl；render；Slang 着色器流水线；效果和材质；lavapipe 进 CI；ui-canvas / canvas-kit；themes/styles；规范 0.3；**openmui-video 的原型，S5 的对标拆解、分镜、动态分镜和风格帧** | **S1 Markdown 墙**和 **S2 风格画廊**；第一份基准报告 |
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
| 自己维护 GPU 抽象层的成本 | 范围严格控制在 UI 需要的程度；第一个后端是 SDL_GPU 的薄映射；每个后端都跑 B 级 CTS |
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
| D-49 | **通用动画引擎 `openmui.anim`**：独立组件 `modules/anim/`，只依赖标准库，不知道 UI 的存在；UI、视频、游戏、画布共用；将来可以独立成仓库 |
| D-50 | S4 改为《组件打砖块：openmui 巡礼》：六个关卡，每一关的玩法本身就是一项框架能力（语义、风格、画布、时间倒流、agent、多语言） |
| D-51 | S5 是 **openmui 宣传片**：约 60 秒主片 + 15 秒短片，双语字幕；用 openmui-video 制作，全部是实时渲染的真实场景；对标三部播放量几百万到上千万的 agent 代码动画视频，拆解、分镜、验收见 promo-video 设计文档 |
| D-52 | 协作流程：单个 PR，每一轮修改一个 commit 作为检查点 |
| D-53 | 每个里程碑、展示、模块都有验收标准；能自动检查的进 CI，不能自动检查的写成检查清单并由维护者签字 |
| D-55 | **独立组件放在 `modules/` 下**（参考 mcpp 的 `modules/`）：它们不依赖 openmui 的任何其他包，随时可以拆成独立仓库；openmui 专用的包留在 spec / runtime / hosts / components 等原来的位置 |
| D-57 | **渲染层抽象也是独立组件**：`modules/render`（显示列表、渲染器接口、CPU 和 GPU 后端、内置着色器），`gfx/` 目录取消；布局引擎 `modules/layout`、图像编解码 `modules/image`、录制回放 `modules/record`、文件和视频输出 `modules/encode` 同样放在 `modules/` 下；这些模块共用一个只依赖标准库的几何模块 `modules/geom`（规范保留自己的线上值类型，运行时在边界处转换）。**平台相关的代码一律经由 `mcpplibs.os`**（文件、目录、进程、管道、IPC），窗口和输入经由 SDL3 |
| D-58 | **GPU 抽象层叫 RHI**：`modules/gpu` 改名为 `modules/rhi`（包 `openmui-rhi`，接口 `omui.rhi`，后端 `rhi-sdl`、`rhi-gles`、`rhi-webgpu`、`rhi-vk`），沿用业界对“渲染硬件接口”的叫法；`modules/render` 保持原名：它是渲染子系统（显示列表、渲染器接口、CPU 和 GPU 渲染器），和 `layout`、`raster`、`text` 一样以子系统命名 |
| D-59 | **可观测性是框架的一部分，不是外挂工具**：独立组件 `modules/trace`（`openmui.trace`：按线程的定长缓冲，每个事件无锁、无分配；关闭时一个 span 只是一次 relaxed 原子读；导出 Chrome trace JSON，在 Perfetto 里打开；按 span 汇总 count / total / p50 / p95 / p99 / max）；日志分类别（`Log{"layout"}`），过滤规则 `layout=debug,*=info` 来自 `OMUI_LOG` 或 `--log`，最近的记录留在环形历史里；运行时记录每一帧各阶段的耗时（timers、input、regions、animations、layout、bounds、paint）和最近 240 帧的历史，慢帧按限频写警告；调试模式（`DebugOptions`）叠加节点边界、重绘闪烁、悬停检查器（盒模型、路径、类型、源码行）和帧时间 HUD；agent 协议增加开发操作 `inspect`、`stats`、`profile`、`logs`、`debug`，`omui agent` 和 `omui::run` 的 `--debug` / `--log` / `--profile` 对应它们。开发辅助只走诊断通道，不改变语义树，像素黄金值只在调试模式关闭时有效 |
| D-60 | **所有可视属性都可以做动画，控制粒度到像素**：契约里数值、长度、颜色、变换、渐变、阴影、路径类型的属性默认可以动画，代价级别由生成器标出；粒度分对象 / 属性、子对象（字形、路径弧长、实例，作为可错峰的域）、像素（Slang 效果、图层、轨道遮罩、按时间绘制的 `painter`）三层，全部是时间的纯函数，可以跳转；颜色一律在 OKLab 插值（§6.3、§6.8） |
| D-61 | **开发工具用 openmui 自己写，同时是示例**：`tools/inspector`（openmui-devtools）通过 agent 协议连接任何正在运行的应用（进程外，几乎不扰动目标），包含节点树和属性面板（`inspect`）、帧时间曲线和阶段堆叠图（`stats`）、火焰图和时间线（`profile` 的 trace，画布 + 虚拟化，10 万个事件流畅）、日志查看（`logs`）、叠加层开关（`debug`）、动画检查器（拖动目标应用的时钟）、像素溯源（对象 ID 缓冲）和“为什么重绘 / 重新布局”的依赖图；它自己的界面也在 headless 下做金图测试，可以检查它自己；作为示例展示画布、虚拟化、时间轴、图表和 agent 协议。需要的协议操作：`clock`（暂停、逐帧、跳转、调速）、`trace` 流式订阅、`signals`（依赖图）。**每个工具都有两种界面，读同一份数据**：命令行（JSON 行输出、退出码，方便 agent 分析和 CI）和用 openmui 写的界面（方便人观察和操作）；每个工具的界面都要**突出框架自己的特色**，让工具本身就是框架的展示：火焰图和事件时间线用画布、虚拟化和动画缩放，节点树用语义树和 agent 协议，动画检查器用可跳转的时钟，基准报告用仪表盘（`tests/bench` 的参考场景本身就是 1000 个节点的仪表盘）。`omui-bench`、`omui agent` 先有命令行，界面随 inspector 落地 |
| D-62 | **增量帧是精确的**：`modules/render` 的 `DamageTracker` 按节点标记把显示列表分段（段的哈希覆盖自己的命令和子节点的顺序，范围在设备坐标里），和上一帧比较，给出可能变化的**整行带**；CPU 光栅器保证一个行带和整张画面的同几行逐字节相同（行边界的交点总是从边自己的端点算，越界的行跳过而不是截断），所以只画这些行就等于完整渲染。测试对每一种变化逐字节比对；M1 ⑤ 的仪表盘单个文字变化的一帧从 28ms 降到 2ms 以内。行带还可以切成任务在线程池上同时画（每个任务一个画布，字形先在调用线程上解析），结果仍然和串行渲染逐字节相同；所有卡片同时动画的一帧从 25.7ms 降到 5ms。GPU 路径用同一个 tracker 做 scissor。光栅器在复用画布时每帧不分配 |
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
| | ② CommonMark 测试集通过率 ≥ 95%（GFM 扩展单独统计） | ✅ `modules/markdown`（md4c 经 `mcpplibs.mwrap.md4c`）：CommonMark 0.31.2 规范 652/652，GFM 扩展、回归和覆盖用例 156/156；808 个例子从文档模型写出的 HTML 和 md4c-html 逐字节相同；三个桌面平台和 wine 上一致 |
| | ③ 动画：A 级曲线金值；随机 1000 个时刻的跳转一致性；被打断的 spring 速度连续（< 1e-9） | ✅ 曲线、spring、decay 的金值在各平台固定；通道保留被打断前的分段作为历史（30 秒），事后跳转到 1000 个随机帧和播放时逐位相同，打断处速度连续；运行时跳转越过被打断的过渡也一样 |
| | ④ 跨平台确定性测试在三个桌面平台上是绿的 | ✅ 每个金图（视图、组件、文本、效果、示例）在 Linux / macOS / Windows（MSVC ABI）和 wine 上是同一个固定哈希 |
| | ⑤ 中文、阿拉伯语、emoji 混排的排版测试集 | ✅ `modules/text`：回退、双向、断行、字素和混排像素固定；带样式的字形（旋转、着色）也固定 |
| | ⑥ 录制一段会话，在 headless 下回放，结果完全相同 | ✅ 示例 07 的“recorded session replays exactly”；会话文件名可以是任何文字（`mcpplibs.os` 0.1.1） |

### 22.4 计划之外提前完成的

- 可观测性（D-59）：`modules/trace`、日志类别、帧阶段耗时、调试叠加层、agent 开发操作。
- 精确的增量帧和并行行带（D-62），稀疏光栅化（全帧 22.8 → 8.0ms）。
- 所有可视属性可做动画（D-60），OKLab 颜色；运行时的时间控制（暂停、单步、调速、跳转）。
- 管线编排（D-66）：`modules/pipeline`，运行时一帧是八个阶段插件，agent 可以在运行中替换。
- F1 / F4 的旗舰作品 Inspector 第一版（D-61、D-63）。
- 细粒度动画的基础设施（D-68 阶段 ①–③）：
  - 字形带来历（源字节、字素、词、行），绘制命令可以带逐字形样式。
  - 任何节点的 `glyph_motion`（逐字形 / 字素 / 词 / 行，错峰、带种子的随机、重复；预设：打字机、升起、聚合、散开、波浪）。
  - 效果注册表（溶解、擦除、显现、位移、噪声、色散、模糊、像素化、粒子，以及程序自己注册的效果），任何节点的 `pixel_effect`。
  - 全部是时间的纯函数：能跳转，在行带并行和增量 damage 下逐字节一致，三个平台金图一致。
  - 组件不需要配合：Markdown 组件的文字直接就能动。GPU 路径（阶段 ④）在 M4。
- Markdown 的文档模型（`modules/markdown`）：块和行内标记、标题锚点、任务位置、块哈希；流式追加只重新解析尾部（稳定边界由 md4c 用哨兵行确认，结果总等于整篇解析；4 字节一个 token 时每次约 1µs）。md4c 按 mwrap 规范做成了 `mcpplibs/md4c-m`（`mcpplibs.mwrap.md4c` 0.5.3，含 md4c-html 和宏映射，上游 808 个例子在四个平台的 CI 上通过），已收录进 mcpp-index（#578）。

### 22.5 还差什么（按计划顺序）

- M2 的验收已完成（见 22.3）。
- M3 进行中：
  - ✅ 表达式 `openmui.expr`（`runtime/expr`）：CEL 兼容的解析器和打印器（往返由随机合法和随机非法输入做模糊测试，四个平台同一个固定哈希）、组合子、同步求值器（错误给 null 和定位诊断，带修复提示）、注册表里的全部函数（函数表从生成的注册表来）；树高不超过 200 层，解析、求值、打印都不会栈溢出。`openmui.unicode` 同时补上了完整的大小写映射（SpecialCasing、Final_Sigma）。
  - ✅ 视图文档 `openmui.viewdoc`（`runtime/viewdoc`）：检查（名字、类型、ID、表达式，带位置和修改建议）、加载成和 builder 相同的 Element、绑定和事件（`set` / `verb` / `emit`，激活上的 verb 走 S-1 / S-2）、`if` / `for`（列表里虚拟化）、插槽、原地重新加载（signal 和节点保留）、组件文档（接口生成契约）、JSON Schema（`spec/schema/view.schema.json`，CI 校验）、规范格式；`omui check` / `fmt` / `schema`；随机文档模糊测试。
  - 示例 03 的 C++ 和视图文档两种写法：语义树和像素完全相同（S 级），保存即重新加载（< 200ms，计数保留）；TS / Python / Lua 版本随绑定加入。
  - `runtime/abi`（C ABI）、`openmui.h` 生成的完整绑定、TS / Python / Lua；`runtime/reflect`；示例 02–06、12。
- M4：`modules/rhi` 和 `rhi-sdl`、GPU 渲染路径、Slang 着色器、效果和背景输入（液态玻璃，D-64）、画布、风格包、S1 / S2。
- M5：openmui-video、ui-timeline、MCP 服务器、skills、S3 / S4 / S5。
- 契约⑦（插件与管线）写进规范；Inspector 的火焰图和动画检查器视图。

## 来源

- GCC 16 的 C++26 反射：[GCC 16.1 released（isocpp.org）](https://isocpp.org/blog/2026/04/gcc-16.1)、[GCC C++ Dialect Options（-freflection）](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)、[cppreference：C++26 编译器支持](https://en.cppreference.com/cpp/compiler_support/26)、[GCC 16.1 发布（Phoronix）](https://www.phoronix.com/news/GCC-16.1-Released)
- 视频工具的性能数据：[Remotion Lambda 成本和耗时示例](https://www.remotion.dev/docs/lambda/cost-example)、[Remotion Render Times（rendercomp）](https://rendercomp.com/blog/remotion-render-time-benchmarks/)、[Remotion Alternatives（Orshot）](https://orshot.com/blog/remotion-alternatives)、[IntelligentMixVideo#251 的实测](https://github.com/HsiangNianian/IntelligentMixVideo/pull/251)、[Remotion Lambda 指南（rendercomp）](https://rendercomp.com/blog/remotion-lambda-cloud-rendering-guide/)、[Revideo：Slow Rendering](https://docs.re.video/common-issues/slow-rendering/)、[yt3#166：Motion Canvas 和 Remotion 的对比](https://github.com/KAFKA2306/yt3/pull/166)、[HyperFrames vs Remotion（ArceApps）](https://arceapps.com/blog/hyperframes-vs-remotion-2026/)
- Remotion 和 agent skills：[Remotion Agent Skills Guide 2026](https://aividpipeline.com/blog/remotion-agent-skills-guide-2026)、[Remotion Skills 2026（gaga.art）](https://gaga.art/blog/remotion-skills/)、[What Is the Remotion Skill?](https://www.ngram.com/blog/remotion-skills-sh-ai-video-creation)
- Opus 5.5 带起的代码动画视频：[The Emergence of Code-Based Animation in Claude Opus 5.5](https://bretkerr.substack.com/p/the-emergence-of-code-based-animation)、[officechai：10 Best Examples](https://officechai.com/ai/claude-opus-5-5-motion-graphic-videos/)、[How to Make an Opus 5.5 Cartoon in Claude Code](https://explainx.ai/blog/how-opus-5-5-viral-videos-are-made-code-not-diffusion-2026)、[Claude Motion 教程](https://help.apiyi.com/en/claude-motion-opus-5-5-prompt-to-motion-video-tutorial-en.html)、[revid.ai：64 Videos + Prompts](https://www.revid.ai/claude-motion-graphics)
- SDL_GPU 的 WebGPU 后端（实验性的 PR）：[libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020)、[libsdl-org/SDL#12046](https://github.com/libsdl-org/SDL/pull/12046)
- 本地：`mcpplibs/os`（分支 `os-0.1.0`）的 README 和 `mcpp.toml`；`mcpp/docs/05-dependencies.md`（git 依赖）、`07-workspace.md`（带根包的 workspace）、`04-mcpp-toml.md`；xim-pkgindex 和 mcpp-index 的包列表
