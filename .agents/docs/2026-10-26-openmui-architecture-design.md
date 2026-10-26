# openmui 总体架构设计

> 状态：**草案 v0.2**（2026-10-26），待 review
> 仓库：`Sunrisepeak/mui`（私有，暂定）。**内部的包名、模块名、命令、文档一律使用 openmui**；等各方面成熟后，再评估是否迁移到 openmui 的公开仓库。
> 配套文档：
> - [规范设计](2026-10-26-openmui-spec-design.md)
> - [组件、绑定与工程模型](2026-10-26-openmui-component-binding-design.md)
> - [画布、Markdown 与旗舰示例](2026-10-26-openmui-canvas-markdown-design.md)
> - [视图与数据文件：`.omui` 语言、视图文档、表达式、行为、后缀体系](2026-10-26-openmui-omui-language-design.md)
> - 前期调研见 `research/`（命名已经过时）
> 写法说明：标注了路径的内容是本地已有事实；其余都是设计提议。示意代码和命令只表达设计意图，接口名还没有定。

---

## 1. 定位

**openmui 是一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现。** 它的核心是一层**统一的 UI 语义**：
- 人看到的像素、读屏器读到的无障碍树、agent 拿到的 JSON、测试里的断言，都是**同一棵语义树**的不同渲染；
- 同一份应用可以跨平台（桌面、移动、Web、嵌入式 Linux、MCU、游戏引擎），**也可以跨框架**（Qt、ImGui、Godot 等框架的应用可以通过适配器接入同一套语义）；
- 核心用 C++ 实现，追求性能；业务逻辑可以用 TS/JS、Python、Lua 编写，脚本层只是一层很薄的接口。

**一句话定位：Agent 原生、可以 headless 测试的现代 C++ UI。**

### 1.1 目标

1. 统一的语义层：节点、动作、效果分级、来源标记、稳定 ID。
2. 一个运行时同时支持立即模式和保留模式，两者产出同一棵树。保留模式可以用各语言原生的 builder，也可以用可选的 `.omui` 视图语言或 `.omui.json` 视图文档。
3. 渲染和宿主都可以替换，用 S / L / X 三种边界区分（见 §4.3）。
4. 组件“契约优先”：C++ 实现，三种语言的绑定自动生成。
5. 由 mcpp.toml 驱动整个工程，构建、运行、测试、打包都走 mcpp；工具和应用的分发走 xlings。
6. 三根支柱做到极致：**性能、Agent 原生、确定性与可追溯**（见 §10）。

### 1.2 非目标

- 不包装其他框架的控件 API。跨框架只做语义适配器，或者互相嵌入。
- 不追求原生控件观感。走自绘一致性路线；需要原生观感时，交给适配器接入的原生框架。
- UI 层不承担场景、物理、音频这类游戏引擎职责。
- 短期不做 C++ 代码的热补丁。
- 第一阶段不碰 xdesk（见 §11）。

---

## 2. 设计原则

| # | 原则 | 含义 |
|---|---|---|
| P1 | **语义是唯一事实来源** | 像素、无障碍、agent、测试都从语义树渲染出来；三者不会互相偏离（借鉴 xlings 文档里的 “core and surfaces are separate”） |
| P2 | **规范由实现驱动** | 先有能跑的参考实现，再从中抽出边界契约；等第二个实现者跑通 CTS，才冻结这一部分 |
| P3 | **机制和策略分离** | 核心只提供高性能的机制；交互、外观、业务都是可以插拔的策略（画布组件是最典型的例子） |
| P4 | **选择放在配置里，不放在 API 里** | 一套控件、一种状态模型、一种布局、一种样式、一种 ID 规则；后端和宿主通过能力协商自动选择 |
| P5 | **C++ 承担每帧的工作，脚本只做决定** | 脚本永远不进入每帧路径 |
| P6 | **契约优先，自动生成** | 组件和原生能力的接口都写成契约；绑定、类型信息、文档、CTS 骨架全部由契约生成 |
| P7 | **官方组件没有特权** | 官方组件只能使用公开 API，第三方组件和官方组件待遇相同 |
| P8 | **版本只增不改** | 规范和清单的字段只会新增；读取方跳过自己不认识的字段（照搬 Luban 规范的做法） |
| P9 | **能声明的就不写回调** | 校验、掩码、过渡、条件显示都用声明式写法，由原生代码执行 |
| P10 | **默认值也要做到极致** | 默认主题好看、默认交互顺手、默认性能达标 |

---

## 3. 总体分层

```
┌─────────────────────────────── 消费者 ──────────────────────────────────────────┐
│ 桌面 / 移动应用 · 游戏（Godot / Unreal / 自研）· MCU 设备 · Agent · 测试 ·（以后：xdesk）│
└─────────────────────────────────────────────────────────────────────────────────┘
      ▲ C++：import openmui.*        ▲ TS / Python / Lua：openmui.h        ▲ 总线 / MCP
┌─────────────────────────────── 生态面 ──────────────────────────────────────────┐
│ 组件 openmui-ui-*：base · data · editor · canvas · markdown · game · embedded      │
│ 主题 openmui-theme-*：design token（颜色 / 字体 / 间距 / 圆角 / 层级 / 动效）· 皮肤  │
│ 语言 openmui-lang-*：ts · py · lua（另外有 wasm 插件沙箱）                         │
│ 工具 openmui-tools：生成器 · openmui.rules · 检查器 · 基准测试 · MCP 服务器         │
├─────────────────────────────── 运行时面（openmui-runtime，C++ Modules）──────────┤
│ 写法：  openmui.im（立即模式）──对账──► 同一棵节点树 ◄── openmui.rt（signals + builder / .omui）│
│ 核心：  树与 ID · 状态与 Verb · 事件、焦点与导航 · 输入法 · 任务系统               │
│ 布局：  flex / grid / 栈 / 滚动 / 虚拟化      文本：富文本、排版、双向文字、回退、msdf │
│ 动画：  时钟 · tween / spring / decay · 过渡 · 时间线      语义：产出、动作、效果、审计 │
├─────────────────────────────── 后端面 ──────────────────────────────────────────┤
│ 渲染 openmui-gfx-*：cpu（参考后端）· sdlgpu · gles · vk · mcu · 引擎宿主            │
│ 宿主 openmui-host-*：sdl3 · opensal · web · godot …                                │
│ 跨框架桥 openmui-bridge-*：imgui · qt · godot-control · atspi · dom                │
├─────────────────────────────── 规范面（openmui-spec）───────────────────────────┤
│ registry → 6 个契约：①语义树 ②视图描述 ③状态与 Verb ④宿主接口 ⑤渲染接口 ⑥C ABI     │
│ Core 配置档 + 扩展（OMUI_EXT_*）· 组件契约 · 动画语义 · .omui 语法 · 清单 schema · CTS │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**依赖规则**（用 lint 检查，照搬 `xlings/tools/lint_layer_deps.sh` 的做法）：

| 包 | 只允许依赖 |
|---|---|
| spec | 标准库（MCU 上是 `mcpplibs.std.freestanding`） |
| openmui（伞包） | runtime、base 组件、默认主题；`rules` / `entry-*` 特性 |
| runtime | spec、C 库、mcpp-index 中的包 |
| gfx / host | runtime 的后端接口 |
| bridge | **只依赖 spec**（这样它才算第二个实现者） |
| component | **只依赖 runtime 的公开 API**，不能依赖任何后端 |
| lang | 只依赖 `openmui.h`（C ABI） |
| app | component、runtime、lang |

不允许循环依赖；组件不能直接调用某个后端；C 头文件只出现在 `native/` 绑定模块里（照搬 `lint_c_headers.sh`）。

---

## 4. 规范和实现的关系

### 4.1 一个名字，内部分层（参考 OpenTelemetry / OpenXR / Vulkan / webgpu.h）

| 层 | 内容 | 先例 |
|---|---|---|
| 规范 | registry、契约、扩展、CTS | Vulkan-Docs、`vk.xml` |
| C ABI | `openmui.h`，由 registry 生成，**所有语言绑定都面向它** | `webgpu.h` |
| 参考实现 | `openmui-runtime` 及各后端 | Monado、Dawn |
| Runtime 可以替换 | 引擎或厂商可以提供自己的 Runtime | OpenXR Runtime |

### 4.2 核心 + 扩展

- **Core**：每个实现都必须支持。包括语义树、视图描述、基础组件契约、flex 布局、文本、键盘焦点、基础动画。
- **扩展 `OMUI_EXT_*`**：按需查询，例如 `gpu_node`、`shader_effect`、`instanced_draw`、`canvas`、`external_texture`、`world_space`、`gamepad_navigation`、`ime`、`native_handle`、`lottie`。
- 应用通过枚举扩展来了解当前 Runtime 的能力，**不靠尝试调用来判断**（opensal §2.3 的规则）。
- 用到逃生口（原生句柄等）时必须通过扩展声明，并且会被 CTS 标记为不可移植。

### 4.3 三种边界（直接沿用 opensal §3 的 S / L / X）

| 边界 | 用于 | 机制 |
|---|---|---|
| **S**（构建时选定） | C++ 应用静态链接参考实现、MCU | concepts / CRTP，零开销，没有虚函数 |
| **L**（加载时绑定） | 桌面插件、脚本绑定 | C ABI、类型擦除的接口 |
| **X**（跨地址空间） | agent、远程显示、检查器 | 总线接口 `openmui.tree@0.1`，所有值都可以跨地址空间传递 |

三种方式都符合同一份规范。

---

## 5. 和 Luban 规范的关系

**本地事实**：Luban 的规范（`xlings/luban/specs/`：opensal、openas、openbus、opencaps、openhal、openwx）**只依赖标准库，规范之间互不引用，由实现把它们连起来**（`luban/README.md`）。

openmui 沿用同样的约定：

| 复用的概念 | openmui 规范里怎么处理 | 实现层怎么处理 |
|---|---|---|
| openas 的效果分级（read / reversible / irreversible / external）、actor 的文本形式、“需要确认时不等待，返回退出码 2 并给出能回答的选项” | **取值和文本形式保持一致，但规范自己定义**，不 import openas | runtime 在执行动作时生成 openas 信封 |
| opensal 的 surface（像素 + 事件、帧节奏、`Text` 输入法、`Region`、`Compressed`） | 宿主契约④**兼容 opensal 的模型** | `openmui-host-opensal` 直接实现 opensal 的 consumer |
| openbus 的路径、接口、交互类型 | 总线接口按 openbus 的约定命名：`openmui.tree@0.1`，路径 `app/<id>/ui` | 由应用层或宿主层发布到总线 |
| opencaps 的能力匹配 | 扩展和配置档的协商沿用它的思路 | — |

**已确定（D-20）**：实现层先用 git 路径依赖 opensal 等规范包；等它们发布到 mcpp-index 后再改成版本依赖。规范层不受影响。

---

## 6. 仓库与包

### 6.1 仓库策略

- **阶段一（现在到规范 1.0，或者出现第二个独立实现者为止）**：规范和实现放在**同一个仓库** `Sunrisepeak/mui`（私有）。理由是规范由实现驱动，两者需要原子地一起修改，CI 每次都对参考实现跑 CTS。
- **阶段二**：把规范、registry、生成的头文件和 CTS 拆成 `openmui-spec` 仓库（参考 openkal、Vulkan-Docs）。各语言绑定只有在出现独立维护者时才拆。
- **迁移到公开仓库**：等各方面成熟后再评估，迁移到 openmui 的公开组织。因为内部一直使用 openmui 命名，迁移只需要改仓库地址。

### 6.2 目录结构

```
mui/                                  （仓库名；内部一律叫 openmui）
├── openmui/                          # 伞包 openmui：再导出模块；rules 特性（openmui.rules）；entry-player / entry-app 特性；templates/
│   ├── src/                          #   伞模块 openmui（再导出 runtime、base 组件、默认主题）
│   ├── rules/                        #   构建插件 openmui.rules
│   ├── entry/                        #   入口：player.cpp / app.cpp（等 mcpp#845 落地后由特性提供）
│   └── templates/                    #   mcpp new 的模板：app-cpp / app-ts / app-py / app-lua / app-mixed / component / native / theme
├── spec/                             # openmui-spec：只依赖标准库，有自己的版本号
│   ├── SPEC.md                       #   契约正文（写法和 opensal / openas 一致）
│   ├── registry/                     #   roles · actions · effects · motion · extensions · components/*.contract.omui.toml · omui.ebnf · view.schema.json · manifest schema
│   ├── include/openmui.h             #   由 registry 生成
│   ├── src/                          #   由 registry 生成的值类型模块（openmui.spec）
│   └── conformance/                  #   CTS：分为 S / A / B / C 四级
├── runtime/                          # openmui-runtime：core · im · rt · layout · text · anim · semantic · jobs · devtools
├── gfx/{cpu,sdlgpu,gles,vk,mcu}/
├── hosts/{sdl3,opensal,web}/
├── bridges/{imgui,qt,godot-control,atspi,dom}/   （后期）
├── components/{base,data,editor,canvas,canvas-kit,markdown}/
├── themes/{tokens,default,compact}/
├── lang/{ts,py,lua}/
├── tools/{gen,omui,lsp,inspector,bench,mcp}/   # gen：registry 生成器；omui：命令行（fmt / check / add）
├── examples/                         # 01–13，见画布和 Markdown 的设计文档
├── docs/                             # 用户文档、llms.txt
├── AGENTS.md
└── .agents/docs/                     # 设计记录（本目录）
```

### 6.3 包的种类（用 `[package.metadata.openmui] kind` 标注）

`spec` · `runtime` · `backend` · `host` · `bridge` · `component` · `native` · `theme` · `lang` · `tool` · `app`

每个目录都是一个独立的 `mcpp.toml` 包，各有各的版本号，tag 按包打（例如 `spec-v0.1.0`、`runtime-v0.1.0`）。

### 6.4 命名约定

| 对象 | 名字 |
|---|---|
| 伞包 | `openmui`（模块 `openmui`，再导出 runtime、base 组件和默认主题） |
| 规范 | `openmui-spec`（模块 `openmui.spec`） |
| 运行时 | `openmui-runtime`（模块 `openmui.core`、`openmui.im`、`openmui.rt`、`openmui.text`、`openmui.anim` …） |
| 后端和宿主 | `openmui-gfx-*`、`openmui-host-*`、`openmui-bridge-*` |
| 组件 | `openmui-ui-*`（模块 `openmui.ui.*`） |
| 构建规则 | **`openmui` 包的 `rules` 特性**，模块 `openmui.rules` |
| 入口 | `openmui` 包的 `entry-player` / `entry-app` 特性（D-26） |
| 模板 | `openmui` 包的 `templates/`：`mcpp new <名字> --template openmui:<模板>`（D-27） |
| 命令行工具 | `openmui`，短名 `omui`（可选的便利工具：fmt / check / add / inspect；**只读写 mcpp.toml，不引入新的工程文件**） |
| 文件 | `<名字>.<种类>.omui.toml`、`*.omui`、`*.omui.json`、`*.omuipkg`（完整规则见 omui-language §7，D-30） |
| 扩展 | `OMUI_EXT_*`（D-25） |
| C ABI | Vulkan 风格，前缀 `omui` / `Omui` / `OMUI_`（D-17、D-25） |
| C++ 命名空间 | `omui`（D-25） |
| 各语言的包 | npm、PyPI、LuaRocks 上都叫 `openmui` |

**对外不用裸的 `mui`**，因为它和 React 的 Material UI（`@mui/*`）重名太严重。仓库名 `mui` 只是暂时的私有仓库名。

---

## 7. 构建与生态

### 7.1 mcpp.toml 是唯一的工程文件

- 构建、依赖、特性、目标都写在 `mcpp.toml` 里。
- openmui 自己的声明放在 **`[package.metadata.openmui]`**。本地 mcpp 文档写明：它是“一个 mcpp 会保留但不解释的表……比如一个框架收集每个库各自贡献了什么”，根包的构建程序可以通过 `mcpp::graph_file()` 读到它（`mcpp/docs/04-mcpp-toml.md`）。
- **不再使用独立的应用清单文件**。早期讨论里的 `openmui.app.toml` 已经取消。
- **规范只定义清单的结构（schema）**，`mcpp.toml` 是这份结构的默认载体。其他构建系统可以把同样的结构放在 JSON 里，规范不绑定 mcpp。

### 7.2 构建插件就是 openmui 包的 `rules` 特性

```toml
[build-dependencies]
openmui = { version = "0.1", features = ["rules"], host-module = true }
```

`openmui.rules` 负责：
1. 收集依赖图上每个包的 `[package.metadata.openmui]`；
2. 把契约编译成 IR，再生成各种产物；
3. 编译 `.omui` / `.omui.json`：类型检查和语义检查；发布时生成 C++ 代码或 `.omuic`，开发时由运行时解释执行、可以热重载；
4. 编译 `.behavior.omui.toml`（行为状态机）；
5. 处理非 C++ 依赖 `[package.metadata.openmui.deps.<lang>]`（D-29）：生成各语言原生的清单，调用 npm / uv，获取 Lua 包，锁文件放在 `lock/`；
6. 烘焙字体、图片、着色器；
7. 打包脚本（TS 用 esbuild 转成 JS，Python / Lua 打包），并生成编辑器配置（tsconfig、.venv、.luarc.json）。

每一步都是一个 mcpp action，按文件指纹做增量构建；生成 C++ 的 action 用 `role = "source"`，生成的代码由声明它的包继续编译（`mcpp/docs/31-authoring-a-rule-package.md`）。

### 7.3 各角色分工

| 角色 | 职责 |
|---|---|
| mcpp | 工程文件、构建、运行、测试、打包；交叉编译；执行规则包；`mcpp emit xpkg` |
| xlings | 安装 mcpp、工具链、SDK、node / esbuild / python / uv 等外部工具（xim 索引中已有；**还没有 luarocks**）；以后提供预编译播放器；隔离环境；应用分发（`xlings install <app>`） |
| openmui.rules | 见 §7.2 |
| mcpp-index | 库级生态：runtime、组件、主题、后端、语言运行时 |
| xpkg 索引 | 应用和运行时插件的分发 |
| npm / PyPI | 只用于“Node 作为宿主”或“Python 作为宿主”的模式；包本身仍然由 mcpp 构建 |

### 7.4 三类插件

| 类型 | 载体 | 例子 |
|---|---|---|
| 构建插件 | mcpp 规则包（特性 + `rule_module`） | `openmui.rules`、着色器和字体的烘焙 |
| 库插件 | mcpp-index 中的包 | 组件、主题、后端、宿主、语言运行时、跨框架桥 |
| 运行时插件 | xpkg，运行时加载 | 脚本插件、Wasm 插件；权限受效果分级约束 |

### 7.5 需要 mcpp 支持的能力

| 能力 | 现状 | 处理 |
|---|---|---|
| 入口由依赖提供（代替 `main`） | `kind = "app"` 要求 `main` 指向本包里的文件 | 已提 issue [mcpp#845](https://github.com/mcpp-community/mcpp/issues/845)；在那之前，模板生成一行的 `src/main.cpp` |
| 规则调用外部工具 | 已经支持：`[xlings]` / `[feature-xlings]` | 直接使用；luarocks 还需要补一个 xpkg，在那之前 Lua 依赖走 git / tarball + sha256 |
| 同一个仓库里每个包单独打 tag、单独发布 | mcpp 有 workspace | M0 时验证 |
| 不编译 C++、直接用“预编译播放器 + 资源包”的产物 | 不支持 | 以后再评估，非必需 |

---

## 8. 渲染与宿主

### 8.1 显示列表

图元：矩形、圆角矩形、路径、图片、字形串、裁剪、变换、图层（不透明度、模糊）、外部纹理。每帧携带脏区。像素格式是预乘 BGRA，和 opensal §4 一致。

### 8.2 后端

| 后端 | 用途 | 依赖（mcpp-index 中已有） | 优先级 |
|---|---|---|---|
| **cpu（参考后端）** | 正确性基准、headless 金图、兜底 | pixman、freetype、harfbuzz、nanosvg、libpng（**借鉴 xdesk.draw 的思路，但不依赖 xdesk**） | M1 |
| **sdlgpu** | 桌面和移动的 GPU 后端（Vulkan / Metal / D3D12） | `compat.sdl3` 3.4.2 | M4 |
| gles | 嵌入式 Linux；以后在合成器内部使用 | glad / EGL | 后期 |
| vk | 自研引擎、需要细粒度控制的场景 | vulkan、VMA | 后期 |
| mcu | 局部帧缓冲 + 脏区 | std-freestanding + 板级包 | 后期 |

### 8.3 宿主

M1 先做 **SDL3 宿主**（桌面）。opensal 宿主、Web（emscripten）、引擎宿主放在后面。

### 8.4 平台服务

文件对话框、通知、托盘、菜单、打开链接、剪贴板、生命周期、权限，单独做成 `openmui-platform`（通过能力查询提供）。**每个服务都标注效果分级**，例如打开链接是 `external`。

---

## 9. 一致性分级

| 级别 | 维度 | 承诺 | 验证方式 |
|---|---|---|---|
| **S** | 语义树（ID、role、动作、效果）、组件行为、verb | 所有平台、后端、写法、语言、实现都完全相同 | CTS 的 H0 / H1 用例，跑在所有目标上 |
| **A** | 布局（取整规则写进规范）、动画（公式写进规范）、文本排版 | 规则相同，只有取整误差 | 布局快照；按时间点精确取帧 |
| **B** | 渲染像素 | 和 CPU 参考后端的差异在容差以内 | 金图：CPU 后端逐字节一致；GPU 后端按容差对比 |
| **C** | 平台习惯：快捷键修饰键、滚动物理、窗口装饰、原生对话框、系统字体回退 | 有意不同，并且写进平台适配表 | headless 下模拟平台来测试 |

---

## 10. 三根支柱与指标

| 支柱 | 目标（需要实测校准） | 旗舰演示 | CI 门禁 |
|---|---|---|---|
| **性能** | 平移缩放时每帧 CPU 小于 1ms；1000 个 Markdown 窗口在核显上达到 120Hz；运行时每帧零分配 | **markdown-wall** | 帧时间 P99、分配次数、启动时间 |
| **Agent 原生** | 不靠坐标就能操作任何控件；每个动作都有效果分级和审计；会话可以录制成语义动作日志，再精确回放 | agent 操作 markdown-wall | 每个组件都有 agent 用例；语义检查 |
| **确定性与可追溯** | CPU 渲染逐字节一致；时间旅行调试；**像素溯源**（任意像素都能追到节点、部件、绘制指令和样式来源） | 用语义日志加时钟记录在 headless 下重现 bug | 跨平台金图逐字节比对、回放一致性 |

---

## 11. 里程碑（第一阶段不涉及 xdesk）

| 里程碑 | 内容 | 验收 |
|---|---|---|
| **M0 骨架** | 仓库结构（含 `openmui` 伞包）；registry 和契约 IR；生成器（`openmui.h`、`.cppm`、`.d.ts`、`.pyi`、Lua 注解）；`openmui.rules` 雏形；`app-cpp` 和 `component` 两个模板；依赖分层和命名的 lint | `mcpp new x --template openmui` 能创建工程；`mcpp build` 能从契约生成全部产物 |
| **M1 核心** | 树和 ID、立即模式、flex 布局、基础文本、CPU 后端、SDL3 宿主、headless H0–H2、语义树和 `act` | 示例 01 和 07 能运行 |
| **M2 文本 + Markdown** | 富文本引擎；接入 md4c；块缓存；语义映射；流式追加 | 示例 08 和 09；公开 CommonMark 测试集的通过率 |
| **M3 声明式 + 三种语言** | signals、各语言的 builder、`.omui` / `.omui.json` 和表达式子集、`omui fmt` / `check`、C ABI、TS / Python（宿主模式）/ Lua、`kind=native`、`deps.<lang>`、其余模板 | 示例 02–05、12；示例 03 的五种写法语义完全一致 |
| **M4 GPU + 画布** | SDL_GPU 后端、实例化 SDF、msdf、图层缓存、任务系统、canvas 和 canvas-kit | 示例 10 和 11；第一份基准报告 |
| **M5 打磨** | 性能调优、检查器（语义树、像素溯源）、基准测试进 CI | 指标稳定，进入回归门禁 |
| 以后 | xdesk 集成（`openwx-view` 的阶段一和阶段二）、MCU、游戏引擎、跨框架桥 | — |

**排期说明**：M2 的富文本是整个项目耗时最长的一项，建议和 M1 并行启动。M4 可以提前做一个最小的 GPU 原型（1 万个矩形加标签），尽早用实测数据校准性能目标。

---

## 12. 风险

| 风险 | 对策 |
|---|---|
| 范围失控 | 每个里程碑都绑定一个示例作为验收；“非目标”写进文档 |
| 富文本、输入法、双向文字、无障碍是自绘框架最容易失败的地方 | 复用成熟的库；从 M1 起就建立测试集（CJK、双向文字、emoji） |
| C++ Modules 工具链不成熟 | 由 mcpp 统一管理；模板放在实现单元里；控制接口单元的体积 |
| 依赖 mcpp 会限制推广 | 提供 SDK 导出（C ABI + 预编译库）给其他构建系统；脚本用户不受影响 |
| 规范变成纸面标准 | 由实现驱动；有第二个实现者才冻结；CTS 公开 |
| 只有一个维护者 | 先写规范、接受多个实现；适配器本身就是第二个实现者 |
| 名字冲突 | 对外只用 `openmui`（检索时没有发现同名项目）；不用裸的 `mui` |

---

## 13. 决策记录

| ID | 决策 |
|---|---|
| D-01 | 项目内部统一使用 **openmui** 命名；暂时放在私有仓库 `Sunrisepeak/mui`，成熟后评估迁移到公开仓库 |
| D-02 | 第一阶段规范和实现放在同一个仓库，但目录、包、版本、依赖严格分开；出现第二个实现者时把规范拆出去 |
| D-03 | 语义树是唯一事实来源，像素、无障碍、agent、测试都是它的渲染 |
| D-04 | openmui 规范自包含：和 openas / opensal 的概念保持取值兼容，但不 import 它们；由实现层把它们连起来 |
| D-05 | 立即模式和保留模式经过对账后产出同一棵树，两种写法共用组件和 ID |
| D-06 | 核心用 C++ Modules 实现，不用异常、不用 RTTI，错误用 `std::expected`，可以在 freestanding 目标上编译 |
| D-07 | 脚本层是一层薄接口；**完整发行版**至少提供 TS/JS、Python、Lua 三种；脚本不进入每帧路径；需要同步求值的绑定用兼容 CEL 的表达式子集（由原生代码求值，不是 JS） |
| D-08 | 契约优先：`*.contract.omui.toml` 和 `.omui` 里的 `component` 声明编译成同一种 IR；mcpp.toml 只放包级声明 |
| D-09 | mcpp.toml 是唯一的工程文件；`openmui` 包的 `rules` 特性就是构建插件；分发走 xlings |
| D-10 | 组件分三层：契约（规范）、行为（官方提供，没有特权）、设计系统（可以替换） |
| D-11 | CPU 是参考后端；GPU 先做 SDL_GPU；宿主先做 SDL3 |
| D-12 | 一致性分为 S / A / B / C 四级 |
| D-13 | 画布组件只提供机制，默认只有平移和缩放；WuXian 风格的交互放在示例层实现 |
| D-14 | Markdown 是官方组件（解析用 md4c），markdown-wall 是旗舰示例和基准测试 |
| D-15 | agent 控制分三层：verb、语义 UI、像素 / 输入 |
| D-16 | 第一阶段完全不改 xdesk |
| D-17 | （原 Q-01）C ABI 用 Vulkan 风格命名；缩写是 `omui`（见 §6.4、D-25） |
| D-18 | （原 Q-02）GPU 第一个后端是 SDL_GPU；GLES 后端放到 M5 之后 |
| D-19 | （原 Q-03）role 集合对齐 AccessKit / ARIA 的子集 |
| D-20 | （原 Q-04）实现层先用 git 路径依赖 Luban 的规范包（opensal 等） |
| D-21 | （原 Q-06）无障碍层先评估 AccessKit 的 C 绑定 |
| D-22 | （原 Q-08）许可证：Apache-2.0 |
| D-23 | （原 Q-09）JS 引擎用 quickjs-ng；TS 用 esbuild 转换（xim 索引里已有 `esbuild`、`node`） |
| D-24 | （原 Q-10）Python 提供两种模式：作为宿主（pip）和嵌入，接口相同；M3 先做宿主模式；宿主模式下主线程运行 C++ 事件循环并释放 GIL，Python 运行在逻辑线程上 |
| D-25 | （原 Q-11）**身份用全名 `openmui`**：包、模块（`openmui.*`）、仓库、总线接口。**需要反复输入的地方用缩写 `omui`**：C++ 命名空间、C ABI（`omui` / `Omui` / `OMUI_`）、`OMUI_EXT_*`、`OMUI_*` 环境变量、命令行短名、脚本里的惯用别名 |

| D-26 | （原 Q-05）入口由框架提供，用 `openmui` 的特性控制：`entry-player`（脚本应用）/ `entry-app`（C++ 应用实现 `omui_app()` 回调，类似 SDL3 的 main callbacks）/ 都不启用（自己写 `main`）。在 mcpp#845 落地之前，模板生成一行的 `src/main.cpp` |
| D-27 | `openmui` 包通过 `templates/` 提供 `mcpp new` 模板：`app-cpp`（默认）、`app-ts`、`app-py`、`app-lua`、`app-mixed`、`component`、`native`、`theme` |
| D-28 | （原 Q-07）**视图文件完全可选**，只有视图 IR 是必需的。保留模式有三种等价、可以混用的写法：原生 builder（C++ / TS，可选 TSX / Python / Lua）、`.omui`、`.omui.json`；立即模式不使用视图文件 |
| D-29 | 非 C++ 依赖写在 `[package.metadata.openmui.deps.<lang>]`，由 `openmui.rules` 处理：生成各语言原生的清单，锁文件放在 `lock/`；Lua 的 C 模块由 mcpp 编译；`manifest = "…"` 作为逃生口 |
| D-30 | 后缀体系：`<名字>.<种类>.omui.toml`（contract / native / behavior / theme）、`.omui`、`.omui.json`、`.omuipkg`、`.omuic`；逻辑代码保持原生后缀 |
| D-31 | `.omui` 的首要要求是**人和 agent 的可读性都很高**：十条可读性原则（omui-language §2）；事件处理只允许修改 signal、调用 verb、发出事件；表达式用兼容 CEL 的子集；行为用数据化的 statechart；格式由 `omui fmt` 唯一确定 |

---

## 14. 待决问题

前面的问题都已经确定（D-17 到 D-31）。自我 review 时新发现的问题：

| ID | 问题 | 当前提议 |
|---|---|---|
| Q-12 | `.omui` 语法的细节：`#id` 的写法，`bind(x)`、`on event:` 等关键字是否要再调整 | 按 omui-language §3 的草案，M3 实现前用真实示例再评审一轮 |
| Q-13 | Web 平台的 GPU 后端：SDL_GPU 目前应该还没有 Web 后端（需要确认） | Web 先用 CPU 后端加 canvas 2D 提交；以后再补 WebGPU 后端 |
| Q-14 | 自动 ID 的规则：保留模式下没有显式 `#id` 的节点用结构化 ID（`<类型>:<序号>`），立即模式用 ID 栈 + 调用位置；**ID 不能依赖显示文本**（spec S-8、S-9） | 采用；lint 要求可交互的节点必须写显式 ID |
