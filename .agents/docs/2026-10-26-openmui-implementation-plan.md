# openmui 实施计划

> 状态：**草案 v1.0**（2026-10-26），待 review
> 依据：[architecture](2026-10-26-openmui-architecture-design.md)（D-01 到 D-37）、[spec](2026-10-26-openmui-spec-design.md)、[component-binding](2026-10-26-openmui-component-binding-design.md)、[view-data-files](2026-10-26-openmui-view-data-files-design.md)、[canvas-markdown](2026-10-26-openmui-canvas-markdown-design.md)。
> 本计划新增的决策是 **D-38 到 D-48**（§20），已经同步到架构文档 §13。
> 写法说明：标注了路径或来源的内容是事实；性能数字都是**目标值**，需要实测校准；命令和 API 是示意。

---

## 0. 一页摘要

| 方面 | 计划 |
|---|---|
| 语言 | **C++23 是基线**（全部模块化，`import std`）；用 GCC 16 时可以选用 C++26（反射、`template for`、`std::inplace_vector`），但只限定在**单独的成员包**里（§1.1） |
| 规则 | **不用宏，用 constexpr**；**头文件只有 `openmui.h`（C ABI）**；核心不用异常和 RTTI；热路径没有虚函数，每帧零分配 |
| 工程 | 一个仓库，多个 **workspace 成员**。每个成员有独立的 `mcpp.toml`，就是将来可以拆分出去的独立仓库（§2） |
| 平台 | OS 服务通过 git 依赖 **`mcpplibs/os`（分支 `os-0.1.0`）**；窗口和输入走 SDL3；平台差异用“清单按目标选择模块分区 + constexpr 常量”来处理（§8） |
| 动画 | **时间的纯函数**：可以任意跳转到某一帧，结果确定；spring 有解析解；交互模式和渲染模式共用同一套动画；**提前到 M2**，因为视频依赖它（§6） |
| 展示 | S1 无限画布 Markdown 墙（缩放、高帧率）· S2 风格化 GPU 渲染 · S3 Agent 操作 · S4 特色小游戏《组件打砖块》· S5 用代码做动画视频（openmui-video，对标 Remotion）（§13） |
| 测试 | 单元测试、CTS 的 S / A / B / C 四级、CPU 金图逐字节比对、GPU 金图（lavapipe）、跨平台确定性、三种语言的语义一致性、性能门禁、模糊测试和 sanitizer（§15） |
| 生态 | 需要补充到 mcpp-index、xim、mcpplibs、mcpp 的内容逐项列出（§17） |
| 里程碑 | M0 骨架 → M1 核心 → M2 文本 + 动画 + Markdown → M3 声明式 + 三种语言 → M4 GPU + 画布 + 风格化 → M5 视频 + 游戏 + Agent → M6 打磨和跨平台（§18） |

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
├── spec/                              # ★ openmui-spec：规范（只依赖标准库）
│   ├── SPEC.md
│   ├── registry/                      #   roles / actions / effects / motion / expr / effect / extensions / profiles / components / schemas
│   ├── include/openmui.h              #   全仓库唯一的头文件（生成）
│   ├── src/                           #   值类型模块 openmui.spec（生成 + 少量手写）
│   └── conformance/{S,A,B,C}/         #   CTS
│
├── runtime/
│   ├── core/                          # ★ openmui-core：树、ID、signal、verb、事件、焦点、语义、arena、时钟
│   ├── jobs/                          # ★ openmui-jobs：任务系统（以后可以作为通用库独立出去）
│   ├── layout/                        # ★ openmui-layout：flex / grid / 栈 / 滚动 / 虚拟化
│   ├── text/                          # ★ openmui-text：排版、断行（UAX#14）、双向文字、回退、编辑、输入法
│   ├── anim/                          # ★ openmui-anim：时钟、曲线、spring 解析解、时间线、过渡（§6）
│   ├── view/                          # ★ openmui-view：视图 IR、对账、立即模式（openmui.im）、保留模式（openmui.rt）、表达式（openmui.expr）、视图文档
│   ├── semantic/                      # ★ openmui-semantic：语义树、动作执行、效果分级、信封、录制和回放
│   ├── agent/                         # ★ openmui-agent：openmui.tree@0.1 本地端点（mcpplibs.os.ipc）、协议
│   ├── platform/                      # ★ openmui-platform：平台服务（依赖 mcpplibs/os）
│   ├── abi/                           # ★ openmui-abi：openmui.h 的实现（C ABI 的对外导出）
│   ├── player/                        # ★ openmui-player：加载清单、视图文档和脚本
│   ├── reflect/                       # ★ openmui-reflect：C++26 反射的易用层（只在 GCC 16 下可选）
│   └── devtools/                      # ★ openmui-devtools：检查协议、遥测、基准测试接口、像素溯源
│
├── gfx/
│   ├── render/                        # ★ openmui-render：UI 渲染器（只写一次，基于 omui.gpu）
│   ├── gpu/                           # ★ openmui-gpu：GPU 抽象层接口
│   ├── gpu-sdl/                       # ★ openmui-gpu-sdl：后端（Vulkan / Metal / D3D12）
│   ├── gpu-gles/                      #   openmui-gpu-gles（M6：嵌入式 Linux、WebGL2）
│   ├── gpu-webgpu/  gpu-vk/           #   以后再做
│   ├── raster/                        # ★ openmui-raster：确定性的 CPU 参考光栅器（以后可以作为通用库独立出去）
│   └── shaders/                       #   内置着色器（Slang）：SDF 图元、文字、模糊、效果模板
│
├── hosts/
│   ├── sdl3/                          # ★ openmui-host-sdl3：桌面（以后包括移动）
│   ├── headless/                      # ★ openmui-host-headless：虚拟 surface、注入时钟、输出 PNG（CI 和视频渲染都用它）
│   └── opensal/  web/                 #   以后再做
│
├── components/
│   ├── base/                          # ★ openmui-ui-base：Core 组件
│   ├── markdown/                      # ★ openmui-ui-markdown
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
│   ├── markdown-wall/  style-gallery/  agent-ops/  widget-breakout/  intro-video/
├── tests/                             # 跨成员的测试：CTS 运行器、e2e、确定性、性能
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
| spec | `openmui-spec` | ✅ 出现第二个实现者时 | 参考 openkal、Vulkan-Docs |
| runtime/jobs | `openmui-jobs` | ✅ 通用库 | 也可以贡献给 mcpplibs |
| runtime/text | `openmui-text` | ✅ 通用库 | C++ 模块化的文本引擎在 C++ 生态里很少见 |
| gfx/raster | `openmui-raster` | ✅ 通用库 | 确定性的 2D 光栅器 |
| gfx/gpu + 各后端 | `openmui-gpu*` | ✅ | UI 尺度的 GPU 抽象层 |
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
  "spec",
  "runtime/*", "gfx/*", "hosts/*", "components/*", "themes/*",
  "video", "lang/*", "tools/*", "examples/*", "showcase/*",
]

[dependencies]
openmui-core     = { path = "runtime/core" }
openmui-view     = { path = "runtime/view" }
openmui-ui-base  = { path = "components/base" }
openmui-theme-default = { path = "themes/default" }

[features.rules]
rule_module = "openmui.rules"
sources     = ["mcpp/rules/*.cppm"]

[features.entry-player]
sources = ["src/entry/player.cpp"]      # mcpp#845 落地后改成入口声明
```

---

## 3. 架构落地

### 3.1 一帧的流水线和线程

```
UI 线程（C++）：
  输入（按帧合并）→ 分发事件 → 执行动作 / verb
  → signal 传播（批量、拓扑排序）→ 对账（立即模式的 build、保留模式的局部重建）
  → 增量布局 → 动画求值（批量、SoA）→ 语义变更 → 显示列表（增量）→ 交给渲染线程
渲染线程：显示列表 → render（批处理、实例化、图层缓存）→ omui.gpu 或 raster → 提交 → 等待帧已显示（帧节奏）
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
spec ← core ← {jobs, layout, text, anim} ← view ← semantic ← agent（另外依赖 platform 的 IPC）
                                         ↑
                    render ← gpu ← gpu-*         raster
                    host-* ← (core, render)      platform ← mcpplibs/os
component ← (view 的公开 API)      lang-* ← openmui.h ← abi
video ← (view, anim, host-headless, render, ffmpeg)
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

生成器本身是一个 C++ 模块化程序（依赖 `marzer.tomlplusplus` 和 `compat.yyjson`），由 `openmui.rules` 在构建期调用。

### 4.3 流程

规范的每一次修改都必须同时提交：registry 的修改、CTS 用例、参考实现的实现，缺一项就不能合并。较大的改动先写 RFC，放在 `.agents/docs/`。

---

## 5. 运行时各模块的实施

| 成员 | 范围 | 关键点 | 里程碑 |
|---|---|---|---|
| core | 树、ID（S-8 / S-9）、signal、verb、事件、焦点和导航、arena、时钟 | SoA + 带代数的句柄；`consteval` 校验 ID 字面量；signal 批量提交；**时钟可以注入**（交互、虚拟、渲染三种） | M1 |
| jobs | 线程池、优先级、取消、帧内的并行 for | 工作窃取；任务不在热路径上分配内存 | M1 |
| layout | flex、grid、栈、滚动、虚拟化 | 增量布局、测量结果缓存、**取整规则**（A 级）、`-ffp-contract=off` | M1–M2 |
| text | HarfBuzz 排版、FreeType 字形、fribidi、**UAX#14 断行**、字体回退、选区和编辑、输入法 | 断行表由 `tools/gen` 从 Unicode 数据生成（不额外依赖 ICU）；排版结果缓存；msdf 图集交给 render | M1–M2 |
| anim | 见 §6 | — | M2 |
| view | 视图 IR、对账、`openmui.im`、`openmui.rt`、`openmui.expr`、视图文档的加载 | 立即模式在没有事件时不调用 build；表达式 AST 由原生代码求值；CEL 子集解析器要做模糊测试 | M1（im）、M3（rt、expr、视图文档） |
| semantic | 产出语义树、执行动作、效果分级、`needs-answer`、Origin、信封、**录制和回放** | 增量变更；虚拟化语义；录制内容是“语义动作 + 时钟” | M1 |
| agent | `openmui.tree@0.1` 本地端点（`mcpplibs.os.ipc`），协议是 JSON lines | snapshot / changes / find / act / wait / capture；actor 的文本形式和 openas 一致 | M1（基础）、M5（完整） |
| platform | 文件、目录、进程、IPC（来自 `mcpplibs/os`）；对话框、剪贴板、托盘（来自 SDL3） | 每个服务都标注效果分级 | M1–M3 |
| abi | 实现 `openmui.h` | 由生成器产出调用桩；不让异常越过边界 | M3 |
| player | 加载清单、视图文档、脚本、native 模块 | `entry-player` 使用它 | M3 |
| reflect | C++26 反射的易用层：`omui::form(settings)`、`omui::model_of<T>()` | **只在 GCC 16 加 `-freflection` 时可用**；C++23 下有由契约生成的替代方案 | M3 之后 |
| devtools | 检查协议、遥测、基准测试接口、像素溯源（对象 ID 缓冲） | 检查器本身就是一个 openmui 应用 | M4–M6 |

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

---

## 7. 渲染与 GPU

| 工作 | 内容 | 里程碑 |
|---|---|---|
| raster（CPU 参考光栅器） | **自己实现定点数的扫描线光栅器**，用解析覆盖率做抗锯齿，保证跨平台逐字节一致（不依赖 pixman 的 SIMD 路径，避免不同平台的取整差异）；字形由 FreeType 光栅化（版本由 mcpp.lock 锁定，关闭 hinting） | M1 |
| render | 显示列表 → 批处理；圆角矩形、边框、阴影用 SDF；msdf 文字图集；图层缓存；脏区；外部纹理 | M4（CPU 路径在 M1 就有） |
| gpu | `omui.gpu` 接口：设备、缓冲、纹理、管线、命令列表、导入外部纹理、能力查询 | M4 |
| gpu-sdl | 第一个 GPU 后端（Vulkan / Metal / D3D12） | M4 |
| shaders | 用 Slang 写内置着色器；`rules-slang` 编译成 SPIR-V / MSL / DXIL（/ GLSL ES / WGSL）；通过反射得到参数；开发期热重载 | M4 |
| 效果（第 ① 级） | 效果模板 `EffectInput`；参数就是带类型的属性，可以做动画；第三方的着色器只能使用模板 | M4 |
| 材质（第 ② 级） | 画布节点类型自己的着色器 | M4–M5 |
| gpu-gles | 嵌入式 Linux 和 WebGL2（D-36） | M6 |
| CI 中的 GPU | headless 下用 **mesa-lavapipe**（xim 中已有）跑 gpu-sdl 的 Vulkan 路径 | M4 |

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
| web | emscripten（emsdk 在 xim 中已有）+ gpu-gles（WebGL2） | M6 |
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

## 12. openmui-video：用代码制作动画视频（对标 Remotion）

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
  → 对账 → 布局 → 动画求值 → 渲染（gpu-sdl 或 lavapipe，也可以用 CPU raster）
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

---

## 13. 展示（showcase）

### S1 无限画布 Markdown 墙（`showcase/markdown-wall`）

- **内容**：画布核心加上应用层自定义的 WuXian 风格交互（工作区分组、两个相机预设、小地图）；1000 个 Markdown 窗口；四个 LOD 档位；流式输出；HUD 用立即模式写。
- **验证的能力**：画布、LOD、晋升、图层缓存、任务系统、富文本、语义虚拟化。
- **指标（目标值）**：1000 个窗口在核显上缩放扫描达到 120Hz，帧时间 P99 < 8.3ms；平移缩放时每帧 CPU < 1ms；清晰化时间 < 300ms。
- **CI**：确定性时钟下的场景脚本（TS、Python、Lua 三种），性能门禁。
- **里程碑**：M4。

### S2 风格化 GPU 渲染画廊（`showcase/style-gallery`）

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

- **内容**：
  - 一个 Markdown 笔记应用加上 Markdown 墙，通过 MCP 交给 agent。任务例如：“把所有带 TODO 的文档整理到一个工作区，再写一份总结”。过程中会用到语义查找和空间查询、移动、读取（内容标记为 `remote`）、删除（`irreversible`，会被拦下，需要用户确认）。
  - **agent 生成一个设置面板的视图文档**，挂载上去，再通过语义树验证结果（生成式 UI 的闭环）。
  - 整个会话被录制下来，在 headless 下精确回放，变成一个测试。
- **CI**：不需要真的调用 LLM。用一个脚本化的“假 agent”按确定的步骤操作，验证协议、效果分级和回放。
- **里程碑**：M5（基础协议在 M1 已经可用）。

### S4 特色小游戏：《组件打砖块》（`showcase/widget-breakout`）

- **玩法**：经典的打砖块，但**每一块砖都是一个真实的 openmui 组件**：按钮、开关、滑块、Markdown 卡片。砖块被击中时播放 Slang 溶解效果，并用 GPU 实例化喷出粒子。道具也是组件，必须“用”了才生效：拖动滑块改变挡板的宽度，打开开关进入慢动作（直接调整动画时钟的倍率）。
- **控制**：鼠标、键盘、手柄都可以；**也可以由 agent 来玩**：挡板是一个语义节点，带 `move_to(x)` 动作，游戏状态都能在语义树里读到。这里展示的是 agent 实时控制。
- **技术**：游戏循环和 HUD 用立即模式，菜单用保留模式；物理按固定步长计算、由种子决定；目标 120Hz。
- **CI**：给定种子和输入，在 headless 下回放，比对最终状态以及若干关键帧的哈希。
- **里程碑**：M5。

### S5 代码动画视频（`showcase/intro-video`）

- **内容**：《openmui 一分钟介绍》**完全用 openmui-video 写成**。片中会实时呈现 S1 的缩放、S2 的风格切换、S4 的游戏片段（复用这些展示的组件和场景），再配上字幕和音乐。
- **加分项**：这段视频由 agent 借助 `skills/openmui-video` 来写，人只给要求并审核。
- **CI**：渲染若干关键帧做金图比对；检查跳转一致性；检查每一帧的语义（文字不重叠、不出界）。
- **里程碑**：M5。

### S6 入门和画廊（`examples/01` 到 `13`）

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
| `bench.yml` | PR（带标签时）/ 每晚 | 性能门禁、报告 |
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
| M4 | 三个桌面平台 | + gpu-sdl（Vulkan / D3D12 / Metal） | lavapipe 用于 CI |
| M6 | + Web | gpu-gles → WebGL2（emsdk、openkal-emscripten） | SDL_GPU 的 WebGPU 后端还只是实验性的 PR，不依赖它 |
| M6 之后 | Android、iOS、Linux aarch64、嵌入式 Linux | gpu-sdl / gpu-gles | — |
| 以后 | MCU | mcu 渲染 | freestanding 配置档 |

---

## 17. 需要补充到生态的内容

### 17.1 已经有的（直接使用）

| 位置 | 内容 |
|---|---|
| mcpp-index | `compat.sdl3`、`compat.harfbuzz`、`compat.freetype`、`compat.fribidi`、`compat.msdfgen`、`compat.md4c`、`compat.quickjs-ng`、`compat.lua`、`compat.ffmpeg` / `ffmpeg.ffmpeg`、`compat.miniaudio`、`compat.spirv-reflect`、`compat.libpng`、`compat.zlib`、`compat.yyjson`、`marzer.tomlplusplus`、`compat.xxhash`、`compat.mimalloc`、`compat.wamr` |
| xim（xlings） | `slang`、`mesa-lavapipe`、`vulkan-loader` / `vulkan-headers`、`glslang`、`spirv-tools`、`esbuild`、`node`、`uv`、`python`、`emsdk` |
| mcpp-plugins | `rules-slang`、`rules-spirv`、`dist`（apk / apple / web / wix / appimage） |
| mcpplibs | `os`（git 依赖，分支 `os-0.1.0`）、`openkal` |

### 17.2 需要新增的

| 位置 | 内容 | 用途 | 时间 |
|---|---|---|---|
| mcpp-index | `compat.cpython`（limited API 的头文件和导入库） | Python 绑定 | M3 |
| mcpp-index | `accesskit-c`（预编译） | 无障碍层（D-21） | M5 |
| mcpp-index | `compat.thorvg` | Lottie（以后的扩展） | 以后 |
| mcpp-index | `compat.dawn` 或 `wgpu-native` | gpu-webgpu | 以后 |
| xim | `luarocks` | Lua 依赖（D-29） | M3 |
| xim | `ffmpeg`（命令行） | 视频的调试和转码 | M5 |
| xim | `openmui-fonts`（Inter、Noto Sans、Noto Sans CJK 子集、等宽字体） | **默认字体固定下来**，保证跨平台一致（A / B 级） | M1 |
| xim | `omui`（命令行）、`openmui-player` | 分发 | M3 |
| mcpplibs（以后贡献） | `mcpplibs.unicode`（UAX#14 / #9 的表和算法）、`openmui-jobs`、`openmui-raster` | 通用库 | 稳定以后 |
| mcpp | #845（入口由依赖提供）；每个成员单独打 tag 和发布（V-02）；按特性开关选择互斥的源文件（如果不支持，再提需求） | 工程体验 | 持续 |

---

## 18. 里程碑

> 时间按一个小团队粗略估计，要随着实际进展调整。多条工作流可以并行。

| 里程碑 | 粗略时间 | 主要交付 | 验收 |
|---|---|---|---|
| **M0 骨架** | 2–3 周 | workspace 和所有 ★ 成员的空包；registry 格式；`tools/gen` 的最小闭环（`openmui.h`、契约 → C++ 分区）；`openmui.rules` 雏形；`app-cpp` 和 `component` 两个模板；lint；CI 骨架（三个平台）；`.xlings.json`；`openmui-fonts` | `mcpp new x --template openmui` 能创建工程并构建通过；三个平台的 CI 是绿色的 |
| **M1 核心** | 6–8 周 | core / jobs / layout（flex）/ 基础文本 / view（im）/ semantic / agent（本地端点 + 命令行）/ raster / host-headless / host-sdl3（Linux）/ ui-base 的前 9 个组件；规范 0.1 的 S 级 | 示例 01 和 07；`omui agent` 能操作示例；CPU 金图逐字节一致 |
| **M2 文本 + 动画 + Markdown** | 6–8 周（**文本可以从 M1 开始并行**） | 富文本、UAX#14、双向文字、编辑和输入法；**anim 全部功能（§6）**；ui-markdown；录制和回放；Windows / macOS | 示例 08 和 09；动画的 A 级用例和跳转一致性；跨平台确定性测试是绿色的 |
| **M3 声明式 + 三种语言** | 6–8 周 | rt / expr / 视图文档；C ABI；TS / Python（宿主模式，abi3）/ Lua；native 模块；`deps.<lang>`；player；其余模板；规范 0.2 | 示例 02–06、12；四种写法加视图文档的语义树一致；三种语言的 CTS 一致 |
| **M4 GPU + 画布 + 风格化** | 8–10 周 | omui.gpu + gpu-sdl；render；Slang 着色器流水线；效果和材质；lavapipe 进 CI；ui-canvas / canvas-kit；themes/styles；规范 0.3 | **S1 Markdown 墙**和 **S2 风格画廊**；第一份基准报告 |
| **M5 视频 + 游戏 + Agent** | 6–8 周 | openmui-video；ui-timeline；动画检查器；MCP 服务器；skills；accesskit | **S3、S4、S5**；视频的跳转一致性和逐帧语义检查 |
| **M6 打磨和跨平台** | 持续 | 检查器（像素溯源）；性能调优和门禁；gpu-gles 和 Web；文档完整；准备公开 | 指标稳定；文档完整；评估迁移到公开仓库 |

### 18.1 M0 的任务清单（可以直接开工）

1. 根目录 `mcpp.toml`（伞包 + workspace），以及各个 ★ 成员的 `mcpp.toml` 和最小的主模块。
2. `spec/registry` 的第一批文件：roles、states、actions、effects、motion（公式）、Button / Slider / TextBox 三份契约。
3. `tools/gen`：registry 和契约 → `openmui.h`、`openmui.spec` 模块、契约的 C++ 分区（CRTP）；带单元测试。
4. `mcpp/rules`：调用生成器的 action（`role = "source"`）。
5. lint 脚本：依赖方向、头文件、宏、命名。
6. CI：三个平台的构建 + 单元测试；`.xlings.json`。
7. 模板：`app-cpp`（带一行的 `src/main.cpp`）、`component`。
8. `openmui-fonts` 的 xpkg 草稿。
9. 验证 V-02（每个成员单独打 tag 和发布）以及“按特性选择互斥源文件”。

---

## 19. 风险与对策

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

## 20. 本计划新增的决策

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

---

## 来源

- GCC 16 的 C++26 反射：[GCC 16.1 released（isocpp.org）](https://isocpp.org/blog/2026/04/gcc-16.1)、[GCC C++ Dialect Options（-freflection）](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)、[cppreference：C++26 编译器支持](https://en.cppreference.com/cpp/compiler_support/26)、[GCC 16.1 发布（Phoronix）](https://www.phoronix.com/news/GCC-16.1-Released)
- Remotion 和 agent skills：[Remotion Agent Skills Guide 2026](https://aividpipeline.com/blog/remotion-agent-skills-guide-2026)、[Remotion Skills 2026（gaga.art）](https://gaga.art/blog/remotion-skills/)、[What Is the Remotion Skill?](https://www.ngram.com/blog/remotion-skills-sh-ai-video-creation)
- SDL_GPU 的 WebGPU 后端（实验性的 PR）：[libsdl-org/SDL#16020](https://github.com/libsdl-org/SDL/pull/16020)、[libsdl-org/SDL#12046](https://github.com/libsdl-org/SDL/pull/12046)
- 本地：`mcpplibs/os`（分支 `os-0.1.0`）的 README 和 `mcpp.toml`；`mcpp/docs/05-dependencies.md`（git 依赖）、`07-workspace.md`（带根包的 workspace）、`04-mcpp-toml.md`；xim-pkgindex 和 mcpp-index 的包列表
