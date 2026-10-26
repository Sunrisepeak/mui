# openmui 总体架构设计

> 状态：**草案 v0.3**（2026-10-26），待 review
> 仓库：`Sunrisepeak/mui`（私有，暂定）。**内部的包名、模块名、命令、文档一律使用 openmui**；等各方面成熟后，再评估是否迁移到 openmui 的公开仓库。
> 配套文档：
> - **[实施计划](2026-10-26-openmui-implementation-plan.md)**（目录树、成员划分、里程碑、展示、CI、生态补充以它为准）
> - [宣传片设计：对标拆解、分镜与验收](2026-10-26-openmui-promo-video-design.md)
> - [规范设计](2026-10-26-openmui-spec-design.md)
> - [组件、绑定与工程模型](2026-10-26-openmui-component-binding-design.md)
> - [画布、Markdown 与旗舰示例](2026-10-26-openmui-canvas-markdown-design.md)
> - [视图与数据文件：代码优先、视图文档、表达式、行为、后缀体系](2026-10-26-openmui-view-data-files-design.md)
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
2. 一个运行时同时支持立即模式和保留模式，两者产出同一棵树。保留模式**代码优先**：布局和逻辑一起用各语言原生的 builder 来写；`*.view.omui.json` 视图文档是给工具和 agent 用的可选数据格式。
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
│ 写法：  openmui.im（立即模式）──对账──► 同一棵节点树 ◄── openmui.rt（signals + builder / 视图文档）│
│ 核心：  树与 ID · 状态与 Verb · 事件、焦点与导航 · 输入法 · 任务系统               │
│ 布局：  flex / grid / 栈 / 滚动 / 虚拟化      文本：富文本、排版、双向文字、回退、msdf │
│ 动画：  时钟 · tween / spring / decay · 过渡 · 时间线      语义：产出、动作、效果、审计 │
├─────────────────────────────── 后端面 ──────────────────────────────────────────┤
│ 渲染：render（UI 渲染器，只写一次）→ omui.rhi 抽象层 → rhi-sdl · rhi-gles · rhi-webgpu · rhi-vk │
│       raster（CPU 参考光栅器）· mcu（这两个不经过抽象层）· 着色器用 Slang               │
│ 宿主 openmui-host-*：sdl3 · opensal · web · godot …                                │
│ 跨框架桥 openmui-bridge-*：imgui · qt · godot-control · atspi · dom                │
├─────────────────────────────── 规范面（openmui-spec）───────────────────────────┤
│ registry → 6 个契约：①语义树 ②视图描述 ③状态与 Verb ④宿主接口 ⑤渲染接口 ⑥C ABI     │
│ Core 配置档 + 扩展（OMUI_EXT_*）· 组件契约 · 动画语义 · 视图文档 schema · 效果着色器接口 · CTS │
└─────────────────────────────────────────────────────────────────────────────────┘
```

**依赖规则**（用 lint 检查，照搬 `xlings/tools/lint_layer_deps.sh` 的做法）：

| 包 | 只允许依赖 |
|---|---|
| spec | 标准库（MCU 上是 `mcpplibs.std.freestanding`） |
| openmui（伞包，仓库根目录） | runtime、base 组件、默认主题；`rules` / `entry-*` 特性 |
| runtime | spec、C 库、mcpp-index 中的包 |
| **modules/***（独立组件） | **标准库、其他 `modules/*` 成员、第三方库；不允许依赖 openmui 的任何其他包**（D-55） |
| modules/rhi-* | 只依赖 `modules/rhi`（`omui.rhi` 接口）和各自的图形库 |
| modules/render | `modules/geom`、`modules/raster`、`modules/rhi`、`modules/image` |
| runtime/platform | **mcpplibs.os**（平台相关的代码只在这里经由 mcpplibs.os 出现） |
| host | runtime 的宿主接口 |
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

**完整的目录树和成员划分以[实施计划](2026-10-26-openmui-implementation-plan.md) §2 为准（D-41）。** 概要如下：

```
mui/                 # 根目录 = openmui 伞包 + workspace
├── mcpp.toml  src/{openmui.cppm, entry/}  mcpp/rules/  templates/
├── spec/                                  # 规范；全仓库唯一的头文件 include/openmui.h
├── modules/{geom,anim,layout,unicode,text,raster,image,render,rhi,rhi-sdl,rhi-gles,record,encode,trace}/
│                                          # 独立组件：不依赖 openmui 的任何包，将来可以直接独立成仓库（D-55、D-57）
├── runtime/{core,view,semantic,agent,platform,abi,player,reflect,devtools}/
├── hosts/{sdl3,headless,opensal,web}/
├── components/{base,markdown,canvas,canvas-kit,timeline,data,editor}/
├── themes/{tokens,default,compact,styles}/   video/   lang/{ts,py,lua}/
├── tools/{gen,omui,mcp,inspector}/   examples/   showcase/   tests/   skills/   docs/
└── .agents/docs/
```

每个目录都是一个 workspace 成员，有独立的 `mcpp.toml`，只通过 `path` 依赖互相引用，**将来可以直接拆成独立仓库**。

**为什么仓库根目录就是伞包**：mcpp 支持“带根包的 workspace”（`mcpp/docs/07-workspace.md` §2.2）。用户写的 `features = ["rules"]`、`--template openmui:app-ts` 都指向 `openmui` 这个包，所以 `rules` 特性、入口特性和 `templates/` 都必须属于它。**`mcpp/` 只放构建系统的代码**；入口是运行时代码，放在 `src/entry/`；播放器在 `runtime/player`（D-32）。

### 6.3 包的种类（用 `[package.metadata.openmui] kind` 标注）

`spec` · `runtime` · `backend` · `host` · `bridge` · `component` · `native` · `theme` · `lang` · `tool` · `app`

每个目录都是一个独立的 `mcpp.toml` 包，各有各的版本号，tag 按包打（例如 `spec-v0.1.0`、`runtime-v0.1.0`）。

### 6.4 命名约定

| 对象 | 名字 |
|---|---|
| 伞包 | `openmui`（模块 `openmui`，再导出 runtime、base 组件和默认主题） |
| 规范 | `openmui-spec`（模块 `openmui.spec`） |
| 运行时 | `openmui-runtime`（模块 `openmui.core`、`openmui.im`、`openmui.rt`、`openmui.text`、`openmui.anim` …） |
| 渲染和宿主 | `openmui-render`（模块 `openmui.render`）、`openmui-rhi`、`openmui-rhi-*`（模块 `openmui.gpu`、命名空间 `omui::gpu`）、`openmui-host-*`、`openmui-bridge-*` |
| 组件 | `openmui-ui-*`（模块 `openmui.ui.*`） |
| 构建规则 | **`openmui` 包的 `rules` 特性**，模块 `openmui.rules` |
| 入口 | `openmui` 包的 `entry-player` / `entry-app` 特性；源码在 `src/entry/`（D-26、D-32） |
| 模板 | `openmui` 包的 `templates/`：`mcpp new <名字> --template openmui:<模板>`（D-27） |
| 命令行工具 | `openmui`，短名 `omui`（可选的便利工具：fmt / check / add / inspect；**只读写 mcpp.toml，不引入新的工程文件**） |
| 文件 | 数据文件一律是 `<名字>.<种类>.omui.{toml,json}`；另有 `*.omuipkg`；着色器 `*.slang`；逻辑代码保持原生后缀（完整规则见 view-data-files §6，D-30） |
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
3. 检查 `*.view.omui.json` 视图文档（schema、名字、类型、ID、语义），发布时嵌入成二进制格式；开发时由运行时直接加载，可以热重载；
4. 编译 `.behavior.omui.toml`（行为状态机）；
5. 处理非 C++ 依赖 `[package.metadata.openmui.deps.<lang>]`（D-29）：生成各语言原生的清单，调用 npm / uv，获取 Lua 包，锁文件放在 `lock/`；
6. 编译着色器：调用 `rules-slang` 把 `*.slang` 编译到各后端的格式，并通过反射得到参数；烘焙字体和图片；
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

### 8.2 渲染：UI 渲染器 + GPU 抽象层（D-33）

**UI 渲染器只写一次**，写在一个“UI 尺度”的 GPU 抽象层 `omui.rhi` 之上；每个平台只需要实现一个比较薄的后端。范围参考 sokol_gfx 和 SDL_GPU 的规模，不做成游戏引擎那种大而全的 RHI。

```cpp
export module openmui.gpu;
export namespace omui::gpu {

struct Caps { bool compute, instancing, msaa, external_texture, timestamps; ShaderFormats formats; };

class Device {                                         // 由各后端实现
public:
    virtual Caps caps() const = 0;
    virtual std::expected<Buffer,   Error> create_buffer(const BufferDesc&) = 0;
    virtual std::expected<Texture,  Error> create_texture(const TextureDesc&) = 0;
    virtual std::expected<Pipeline, Error> create_pipeline(const PipelineDesc&) = 0;   // 着色器 + 顶点布局 + 混合
    virtual std::expected<Texture,  Error> import_external(const ExternalDesc&) = 0;   // dmabuf / IOSurface / D3D 共享句柄
    virtual CommandList begin() = 0;                                                   // render pass、实例化绘制、可选的 compute
    virtual void submit(CommandList&&, Target&) = 0;
    virtual ~Device() = default;
};

}
```

| 渲染路径 | 用途 | 依赖（mcpp-index 中已有） | 优先级 |
|---|---|---|---|
| **raster（CPU 参考光栅器）** | 正确性基准、headless 金图、视频的 CPU 渲染、兜底；不经过抽象层 | **自己实现的定点光栅器**（解析覆盖率抗锯齿，跨平台逐字节一致，D-43）；字形用 freetype、harfbuzz；图片用 libpng、nanosvg（借鉴 xdesk.draw 的思路，但不依赖 xdesk） | M1 |
| **rhi-sdl** | 第一个 GPU 后端，覆盖桌面和移动（Vulkan / Metal / D3D12） | `compat.sdl3` 3.4.2 | M4 |
| **rhi-gles** | 嵌入式 Linux、**Web（emscripten 把 GLES 3 映射到 WebGL2）**、以后在合成器里使用 | glad / EGL | M6 |
| rhi-webgpu | Web 的现代 GPU 路径；也可以把 wgpu-native / Dawn 作为这个后端的实现 | — | 后期 |
| rhi-vk | 自研引擎、合成器、需要细粒度控制的场景 | vulkan、VMA | 后期 |
| mcu | 局部帧缓冲 + 脏区；不经过抽象层 | std-freestanding + 板级包 | 后期 |

### 8.3 自定义着色器（D-34）

- **着色器语言用 Slang**：写一次，编译成 SPIR-V / MSL / DXIL / GLSL ES / WGSL。本地 `mcpp-plugins` 已经有 `rules-slang` 和 `rules-spirv`；参数通过反射得到（`compat.spirv-reflect`）。WGSL 和 GLSL ES 的输出质量要在 M4 之前验证，不行就回退到 SPIRV-Cross / Naga 来转换。
- **自定义渲染分四级**：
  1. 效果：节点或图层上的片段着色器，`OMUI_EXT_shader_effect`；
  2. 材质：画布节点类型自己的顶点和片段着色器，`OMUI_EXT_instanced_draw`；
  3. GPU 节点：通过 `omui.rhi` 完全接管一个 render pass，`OMUI_EXT_gpu_node`；
  4. 原生句柄：逃生口，不可移植，`OMUI_EXT_native_handle`。
- **着色器参数就是带类型的属性**：可以绑定，也可以由原生代码驱动动画。
- **开发期热重载**：着色器文件改动后重新编译、替换管线。
- **测试**：CPU 参考光栅器不执行自定义着色器（按“不带效果”绘制并给出诊断）；GPU 后端的金图测试在 headless 下用 **lavapipe（软件 Vulkan）**来跑，mcpp 的 adapter 文档里正好提到了 lavapipe 的 ICD。
- **安全**：第三方插件的着色器只能使用“效果模板”（固定的输入和输出，不能改管线状态），并且必须经过编译器校验。

### 8.4 宿主

M1 先做 **SDL3 宿主**（桌面）。opensal 宿主、Web（emscripten，渲染走 rhi-gles 映射到 WebGL2，D-36）、引擎宿主放在后面。

### 8.5 平台服务

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

## 11. 里程碑

**以[实施计划](2026-10-26-openmui-implementation-plan.md) §18 为准。** 概要：

| 里程碑 | 主要内容 | 对应的展示 |
|---|---|---|
| M0 骨架 | workspace、registry、生成器、rules、模板、lint、CI | — |
| M1 核心 | core（任务调度用 taskflow）/ layout / 基础文本 / 立即模式 / 语义 / agent 端点 / CPU raster / headless + SDL3 | 示例 01、07 |
| M2 文本 + 动画 + Markdown | 富文本、**动画全部功能**（视频依赖它）、Markdown、录制和回放、三个桌面平台 | 示例 08、09 |
| M3 声明式 + 三种语言 | rt / 表达式 / 视图文档、C ABI、TS / Python / Lua、native、player | 示例 02–06、12 |
| M4 GPU + 画布 + 风格化 | omui.rhi + rhi-sdl、render、Slang、canvas | S1 Markdown 墙、S2 风格画廊 |
| M5 视频 + 游戏 + Agent | openmui-video、timeline、MCP、skills | S3 Agent 操作、S4《组件打砖块：openmui 巡礼》、S5 openmui 宣传片 |
| M6 打磨和跨平台 | 检查器、性能门禁、rhi-gles / Web、文档 | — |

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
| D-08 | 契约优先：`*.contract.omui.toml`（C++ 组件）和带 `interface` 的视图文档（语言中立的组合组件）编译成同一种契约 IR；mcpp.toml 只放包级声明 |
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
| D-28 | **视图文件完全可选**，只有视图 IR 是必需的。保留模式**代码优先**：原生 builder（C++ / TS，可选 TSX / Python / Lua），布局和逻辑写在一起；`*.view.omui.json` 视图文档是可选的数据格式，可以和代码混用；立即模式不使用视图文件 |
| D-29 | 非 C++ 依赖写在 `[package.metadata.openmui.deps.<lang>]`，由 `openmui.rules` 处理：生成各语言原生的清单，锁文件放在 `lock/`；Lua 的 C 模块由 mcpp 编译；`manifest = "…"` 作为逃生口 |
| D-30 | 后缀体系：数据文件一律是 `<名字>.<种类>.omui.{toml,json}`（view / contract / native / behavior / theme / effect）；另有 `.omuipkg`；着色器 `.slang`；逻辑代码保持原生后缀 |
| D-31 | **人和 agent 的可读性都要很高**：可读性主要由宿主语言保证；builder 和数据文件遵守 view-data-files §2 的八条要求（名字和契约一致、显式 ID、事件的后果可见、类型明确、诊断支持 `--json`、文件自带版本、JSON Schema、llms.txt） |

| D-32 | **仓库根目录就是 `openmui` 伞包 + workspace**。`mcpp/` 只放构建系统的代码（`mcpp/rules` = `openmui.rules`）；入口是运行时代码，放在 `src/entry/`；播放器在 `runtime/player`；`templates/` 按 mcpp 的约定放在根目录 |
| D-33 | **GPU 抽象层 `omui.rhi`**（UI 尺度）：UI 渲染器只写一次；后端顺序是 rhi-sdl → rhi-gles → rhi-webgpu / rhi-vk；CPU 参考光栅器和 MCU 不经过抽象层；抽象层是实现细节，不属于规范 |
| D-34 | **着色器用 Slang**（`rules-slang`）；自定义渲染分四级（效果、材质、GPU 节点、原生句柄）；着色器参数就是带类型的属性；headless 下用 lavapipe 测试 GPU 后端；规范只规定效果着色器的接口（Slang 源码，SPIR-V 作为交换格式） |
| D-35 | （原 Q-14）自动 ID：保留模式用结构化 ID（`<类型>:<序号>`），立即模式用 ID 栈 + 调用位置；**ID 永远不依赖显示文本**；可交互的节点必须写显式 ID（spec S-8、S-9） |
| D-36 | （原 Q-13）Web 先通过 rhi-gles 映射到 WebGL2，以后再补 rhi-webgpu；不依赖 SDL_GPU 将来是否提供 Web 后端 |
| D-37 | （原 Q-12）**取消 `.omui` 视图语言**：代码优先，符合当前的趋势（SwiftUI / Compose / Flutter / React）；给机器用的视图文档是 JSON；表达式的写法是“代码里用组合函数 + 数据文件里用 CEL 字符串”，两者编译成同一个 AST |
| D-38 | **不用宏，用 constexpr**：平台和特性用“清单按目标选择模块分区 + constexpr 常量 + `if constexpr`”；编译期校验用 `consteval`；C 宏在绑定模块里转换；只有生成的 `openmui.h` 是例外 |
| D-39 | C++23 是基线；C++26 特性（反射等）只在单独的成员 `openmui-reflect` 里使用（GCC 16，`-freflection`），核心不依赖它 |
| D-40 | 全项目只有一个头文件 `openmui.h`（C ABI）；其他全部是模块；第三方头文件只出现在 `native/` 实现分区里 |
| D-41 | 一个仓库，多个 workspace 成员；每个成员有独立的 `mcpp.toml`、版本、测试，只通过 `path` 依赖互相引用，可以随时拆成独立仓库 |
| D-42 | OS 服务通过 git 依赖 `mcpplibs/os`（分支 `os-0.1.0`）；窗口和输入走 SDL3 |
| D-43 | CPU 参考光栅器自己实现（定点数、解析覆盖率），保证跨平台逐字节一致 |
| D-44 | 动画是时间的纯函数（spring、decay 用解析解），可以任意跳转、结果确定；交互模式和渲染模式共用；核心动画提前到 M2 |
| D-45 | 新增 openmui-video（用代码制作动画视频，对标 Remotion）和配套的 agent skill |
| D-46 | Python 绑定用 CPython 的稳定 ABI（abi3）自己实现，不用 pybind11 |
| D-47 | UAX#14 断行表由 `tools/gen` 从 Unicode 数据生成，不依赖 ICU |
| D-48 | 默认字体通过 `openmui-fonts` 固定下来，作为渲染确定性的一部分 |
| D-49 | 通用动画引擎 `openmui-anim`：独立的 workspace 成员 `modules/anim/`，只依赖标准库；UI、视频、游戏、画布共用；将来可以独立成仓库 |
| D-50 | S4《组件打砖块：openmui 巡礼》：六个关卡，每一关的玩法本身就是一项框架能力 |
| D-51 | S5 openmui 宣传片：约 60 秒主片 + 15 秒短片，用 openmui-video 制作；对标三部播放量几百万到上千万的 agent 代码动画视频（promo-video 设计文档） |
| D-52 | 协作流程：单个 PR，每一轮修改一个 commit 作为检查点 |
| D-53 | 每个里程碑、展示、模块都有验收标准（实施计划 §19）；能自动检查的进 CI |
| D-55 | **独立组件放在 `modules/` 下**（参考 mcpp 的 `modules/`）：anim、unicode、text、raster、gpu 和各个 gpu 后端。它们不依赖 openmui 的任何其他包，只依赖标准库、彼此和第三方库，所以随时可以拆成独立仓库；openmui 专用的包（spec、runtime、host、组件、video、lang）留在原来的位置 |
| D-57 | 渲染层抽象（`modules/render`）、布局（`modules/layout`）、图像（`modules/image`）、录制回放（`modules/record`）、文件和视频输出（`modules/encode`）都是独立组件，共用几何模块 `modules/geom`；`gfx/` 取消；平台相关的代码一律经由 `mcpplibs.os`（实施计划 §21） |
| D-56 | mcpp-index 里已经有知名、跨平台的库时优先使用（JSON：nlohmann.json；TOML：tomlplusplus；任务：taskflow；队列：concurrentqueue；PNG：libpng；Markdown：md4c；排版：HarfBuzz / FreeType / fribidi）；只有生态里没有、或者确定性要求无法满足时才自己实现（实施计划 §21） |
| D-54 | openmui-video 的渲染速度、增量渲染（四层缓存）、GPU 和硬件编码、预览和可再编辑性都作为指标，每一项都要优于业界最好的工具（实施计划 §12.6） |
| D-58 | GPU 抽象层叫 RHI：`modules/gpu` 改名为 `modules/rhi`（后端 `rhi-sdl` 等）；`modules/render` 是渲染子系统，保持原名（实施计划 §21） |
| D-59 | 可观测性是框架的一部分：`modules/trace`（span、计数器、Chrome trace、百分位汇总）、分类别的日志和过滤、每帧各阶段耗时和历史、调试叠加层（边界、重绘、检查器、HUD），agent 开发操作 `inspect` / `stats` / `profile` / `logs` / `debug`（实施计划 §21） |
| D-60 | 所有可视属性都可以做动画；粒度分对象 / 属性、子对象（字形、路径弧长、实例）、像素（Slang 效果、图层、遮罩）三层，全部是时间的纯函数，可以跳转（实施计划 §6.3、§6.8） |
| D-61 | 开发工具（检查器、帧时间、火焰图、日志、动画检查器、依赖图）用 openmui 自己写，经 agent 协议连接任何应用，同时作为示例（实施计划 §21） |
| D-62 | 增量帧是精确的：`DamageTracker` 给出变化的整行带，CPU 光栅器保证行带和完整渲染逐字节相同，只画这些行（实施计划 §21） |
| D-63 | 每个作品都是双视角：真实可用，同时展示一项特色；有招牌镜头，可以确定地重现（[特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md) §1、§2） |
| D-64 | 液态玻璃是效果组件，建立在背景输入、形状 SDF、效果组、模糊金字塔、damage 依赖这些通用能力之上；语义不变（同上 §3） |
| D-65 | 可拔插架构：核心只规定组件模型、数据契约和管线（契约⑦）；其他都是插件，按阶段或按批调用（同上 §4） |
| D-66 | 管线编排：一帧是 `stage` 插件组成的 DAG，用 `*.pipeline.omui.toml` 描述；H0 到 H3、视频、CPU / GPU 都是不同的管线（同上 §4.4） |
| D-67 | 开发工具共用一个 openmui 外壳，可以单独运行或嵌入目标应用；每个工具都有命令行（同上 §5） |

---

## 14. 待决问题

目前没有阻塞设计的待决问题。下面是实现过程中需要验证的事项：

| ID | 事项 | 什么时候验证 |
|---|---|---|
| V-01 | Slang 输出 WGSL / GLSL ES 的质量 | M4 之前 |
| V-02 | mcpp 的 workspace 能不能每个包单独打 tag、单独发布 | M0 |
| V-03 | mcpp#845（入口由依赖提供）的进展；在那之前用一行的 `main.cpp` 过渡 | 持续跟进 |
| V-04 | lavapipe 在 CI 里运行 GPU 后端金图测试的速度和稳定性 | M4 |
| V-05 | 表达式的组合函数在四种语言里的命名和手感（例如 Python 的 `not_`） | M3 |
| V-06 | ~~mcpp 能否按特性开关选择互斥的源文件；依赖图里有 C++26 成员时 BMI 的兼容性~~ **已确认 mcpp 支持**（M0 用一个最小示例作为样板） | — |
| V-07 | GCC 16 的 `-freflection` 在 `openmui-reflect` 里的稳定性 | M3 |

[特色与管线](2026-10-27-openmui-features-plugins-pipeline-design.md) 提出了四个待决问题，都不阻塞当前的工作：

| ID | 问题 | 什么时候决定 |
|---|---|---|
| Q-15 | 效果的 CPU 参考实现：Slang 的 CPU 目标，还是手写参考实现 | M4 之前做原型比较 |
| Q-16 | 插件动态加载的 ABI 稳定性承诺；第三方效果插件是否需要进程级隔离 | M5 |
| Q-17 | 管线描述只允许应用级配置，还是允许组件在子树里插入阶段 | M2（管线落地时） |
| Q-18 | 多个液态玻璃元素的背景截断：按效果组合并，还是允许用上一帧的背景 | M4 |
