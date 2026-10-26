# openmui 画布、Markdown 与旗舰示例设计

> 状态：**草案 v0.2**（2026-10-26），待 review
> 范围：`openmui-ui-canvas`（高性能画布核心）、`openmui-ui-canvas-kit`（可选行为）、`openmui-ui-markdown`、旗舰示例和基准测试 **markdown-wall**。
> 约束：**完全不碰 xdesk**；只借用 WuXian 的交互**概念**（工作区分组、相机、两种视图、小地图），而且这些都放在示例层实现，不进组件。

---

## 1. 画布：机制和策略分离

### 1.1 分层

```
┌──────────────────────────────────────────────────────────────────────┐
│ 应用的自定义交互（例如 markdown-wall 里的 WuXian 风格）                  │  ← 属于示例
├──────────────────────────────────────────────────────────────────────┤
│ openmui-ui-canvas-kit（可选，按需启用）：选择 · 拖动 · 缩放手柄 · 吸附和对齐线 │
│                                     · 连线 · 小地图 · 网格               │
├──────────────────────────────────────────────────────────────────────┤
│ 默认行为（只有这些）：平移 · 以光标为中心缩放 · 惯性 · 缩放范围             │
├──────────────────────────────────────────────────────────────────────┤
│ openmui-ui-canvas 核心（机制，追求极致性能）                              │
│   SoA 场景存储 · 空间索引 · 相机（double 精度，以相机为原点渲染）           │
│   LOD 档位 · 组件晋升和降级 · 实例化 SDF / 图层缓存 · 命中测试               │
│   手势竞争 · 语义虚拟化                                                  │
└──────────────────────────────────────────────────────────────────────┘
```

### 1.2 扩展点

| 扩展点 | 能自定义什么 | 默认值 |
|---|---|---|
| 节点类型 | 数据列（SoA）、每个 LOD 档位怎么画（实例化图元 / 自定义绘制 / 纹理 / 晋升为某个组件）、命中形状、语义映射 | 矩形节点 |
| 行为 | 输入状态机：接收世界坐标下的事件和命中结果，调用相机和场景的 API | 平移、缩放 |
| 手势竞争 | 多个行为争同一个手势时的裁决（参考 Flutter 的 gesture arena） | 按优先级 |
| 相机控制器 | 约束、spring 动画、**命名的相机预设**（数量不限） | 自由相机 |
| 图层 | 背景、内容、浮层、HUD（屏幕坐标）；可以用显示列表或 GPU 节点自定义 | 只有内容层 |
| LOD 策略 | 档位阈值、每种节点类型的档位链、缓存预算 | 通用阈值 |
| 空间查询 | 公开给应用、行为、脚本、agent | — |
| 语义贡献 | 节点类型声明 role；**行为声明它带来的动作**（例如启用拖动后才有 `move`，效果为 reversible） | 只有视口动作 |

### 1.3 行为的两种写法

**C++ 行为**（每帧都要执行的复杂交互）：

```cpp
export namespace omui::canvas {

struct PointerEvent { Vec2 screen; DVec2 world; Buttons buttons; Modifiers mods; std::optional<NodeRef> hit; };

class Behavior {
public:
    virtual Claim on_pointer(Ctx&, const PointerEvent&) = 0;   // 参与手势竞争：claim / pass
    virtual void  on_wheel(Ctx&, const WheelEvent&) {}
    virtual void  on_key(Ctx&, const KeyEvent&) {}
    virtual void  contribute(SemanticCtx&) {}                   // 声明这个行为带来的动作
    virtual ~Behavior() = default;
};

}
```

**声明式行为**：用数据化的 statechart 文件 `*.behavior.omui.toml` 描述，由 C++ 执行。脚本和 `.omui` 的用户也能定义交互，但每帧的工作不进入脚本。格式见 [omui-language](2026-10-26-openmui-omui-language-design.md) §6：

```toml
# group-drag.behavior.omui.toml
[behavior.group-drag]
on      = "canvas"
initial = "idle"

[behavior.group-drag.states.idle]
on.pointer_down = { when = "hit.kind == 'group.label'", to = "dragging", do = ["capture()"] }

[behavior.group-drag.states.dragging]
on.pointer_move = { do = ["move(hit.node, delta.world)"] }                     # 每帧在 C++ 里执行
on.pointer_up   = { to = "idle", do = ["commit('group.moved', hit.node)"] }     # 只在提交时通知脚本
```

### 1.4 注册节点类型（示意）

```cpp
canvas.register_kind("md.window", {
    .columns  = {"title", "doc_id"},
    .tiers    = { tier::rect(), tier::thumbnail(), tier::layer(), tier::promote<MarkdownWindow>() },
    .hit      = hit::rect_with_titlebar(28),
    .semantic = { .role = "window", .name_from = "title" },
});
```

### 1.5 渲染技术

| 内容 | 技术 |
|---|---|
| 节点框（圆角、边框、阴影、选中高亮） | SDF 片段着色器 + 实例化：同一种图元一次 draw call |
| 连线 | GPU 细分或 SDF 曲线；按 LOD 降低段数 |
| 文字 | msdf 图集；小于阈值时退化为灰条（greeking） |
| 缩略图 / 图片 | 纹理图集 + mipmap |
| 静态的复杂内容 | 瓦片 / 图层缓存；快速缩放时先拉伸已有纹理，停下以后再清晰化 |
| 小地图 | 同一个场景，第二个相机，最低 LOD |
| 10 万以上的规模 | 可选：用 compute 着色器做裁剪和 LOD |

**精度**：世界坐标在 CPU 上用 double；每帧以相机为原点换算成 float 交给 GPU；离原点很远时空间索引分块并重新定基。

**每帧的流程**：输入（按帧合并）→ 相机 → 查询可见集合（O(可见数)）→ 和上一帧做差 → LOD 跨档（分摊到多帧）→ 晋升 / 降级 → 实例化绘制（相机是 uniform）→ 和帧节奏对齐。

**120Hz 下的预算（目标值）**：平移缩放的帧 CPU 小于 1ms；内容有变化的帧 CPU 小于 3ms；GPU 小于 4ms（核显）。

### 1.6 语义：`OMUI_EXT_canvas`

- 虚拟化语义树：只为可见、聚焦、选中的节点生成完整的语义节点；其余通过查询按需生成。
- 空间查询：`find({in_rect, near, name, role, kind})`，**覆盖整个场景**。
- 视口动作：`pan_to` / `zoom_to` / `fit` / `focus_node` / `camera_preset`。
- 行为贡献的节点动作：`move` / `connect` / `delete`（irreversible）…

### 1.7 脚本怎么参与

```ts
canvas.model.push({ x: Float64Array, y: Float64Array, w: Float32Array, h: Float32Array,
                    kind: Uint16Array, title: stringTable });          // 一次调用交付上万个节点
canvas.on("group.moved", ({ node }) => save(node));                    // 只在提交时调用
```

---

## 2. Markdown 组件：`openmui-ui-markdown`

### 2.1 定位

官方组件包，单独发布，不放进伞包的默认依赖。它同时是 **`openmui.text` 富文本能力的验收场景**。

### 2.2 处理流水线

```
源文本 ──md4c（CommonMark + GFM）──► 块 / 行内 IR
       └─ 流式追加：只从“最后一个稳定的块边界”开始重新解析（只重做尾部）
IR ──► 块布局（段落 / 标题 / 列表 / 引用 / 代码块 / 表格 / 图片）
       = 布局盒 + 富文本段落（HarfBuzz、UAX#14 断行、fribidi、CJK、字体回退）
   └─ 块级布局缓存：键是（块内容哈希、宽度、主题版本）
布局 ──► 显示列表（msdf 文字；代码块背景、表格线用 SDF）
     ──► 语义树（heading 带级别、link、list / listitem、table / cell、code、带 alt 的图片）
```

- **解析器用 `md4c`**：mcpp-index 中已有 `compat.md4c`；C 实现，速度快，MIT 授权，符合 CommonMark，SAX 风格（回调式）。
- **缩放不触发重新布局**：布局用逻辑单位；缩放只是变换，只影响光栅缓存。
- **长文档按块虚拟化**：只布局可见的块，其余用估算高度。

### 2.3 扩展点

- **自定义围栏块**（` ```chart `、` ```mermaid `、` ```math `）：由组件包或脚本注册渲染器，渲染器返回视图或显示列表。
- **代码高亮**：内置一个简单的高亮器；完整的高亮做成插件接口。
- **链接**：verb `link.open`，效果为 **external**（agent 点击时需要确认或事先授权）。
- **Origin**：默认是 `user_content`；内容来自远端或 LLM 时设为 `remote`。

### 2.4 契约

```toml
# contract/markdown.contract.omui.toml
[contract.markdown]
role = "document"
spec = "0.1"

[contract.markdown.props]
source     = { type = "string" }
base_url   = { type = "string" }
origin     = { type = "enum", values = ["app", "user_content", "remote"], default = "user_content" }
extensions = { type = "string[]", default = ["gfm"] }
selectable = { type = "bool", default = true }

[contract.markdown.methods]
append           = { args = { chunk = "string" } }     # 流式追加
scroll_to_anchor = { args = { anchor = "string" } }

[contract.markdown.events]
link_activated  = { args = { href = "string" } }
task_toggled    = { args = { index = "u32", checked = "bool" } }
heading_visible = { args = { anchor = "string" }, coalesce = "frame" }

[contract.markdown.actions]
open_link   = { effect = "external", answer = "yes" }
toggle_task = { effect = "reversible" }
copy        = { effect = "read" }
find        = { effect = "read", args = { query = "string" } }
```

---

## 3. 旗舰示例和基准测试：markdown-wall

### 3.1 场景

```
openmui-ui-canvas
├── 工作区分组：示例里自定义的节点类型 group（底板、标签、颜色）
│     └── N 个 md.window 节点（标题栏、边框、内容槽位 → openmui-ui-markdown）
│            文档内容混合：README 风格、长文档、大量代码、大量中文、表格、图片
├── 示例里自定义的行为：GroupDrag、WindowDrag；从 kit 中启用：选择、小地图
├── 两个相机预设：“1:1 聚焦”和“总览”，之间用 spring 过渡
└── HUD：立即模式写的性能面板（帧时间曲线、各档位的窗口数、缓存命中率、后台队列长度）
```

**可选**：读取一份 `wuxian.json`（openwx 的状态格式）来摆放工作区和窗口。这只是应用层依赖 openwx 规范，不涉及任何 xdesk 代码。

### 3.2 窗口的 LOD 档位

| 档位 | 条件（示意） | 绘制内容 | 每帧代价 |
|---|---|---|---|
| T0 远景 | 屏幕上小于约 40px | 色块 + 标题条（实例化 SDF） | 约为 0 |
| T1 中景 | 约 40–300px | 后台低分辨率光栅化的缩略图（带 mipmap 的图集） | 一次纹理采样 |
| T2 近景 | 大于约 300px，没有交互 | 缓存的全分辨率图层纹理；内容变化或跨档时在后台重新生成 | 一次纹理采样 |
| T3 交互 | 聚焦的窗口，或占屏幕很大一部分的窗口 | 晋升为实时的 Markdown 组件（可以选择、滚动、点击链接） | 正常组件的开销，但同时只有少数几个 |

档位切换时：各档位用同一份布局和同一个主题，切换时短暂地交叉淡化。

### 3.3 后台任务和预算

- 线程池负责解析、布局、缩略图光栅化、图片解码。
- 优先级按“屏幕上的面积 × 离视口中心的距离”排序；**每帧上传纹理有预算**（例如 2ms）。
- 快速缩放时先拉伸已有纹理，停下来以后再逐步清晰化（指标：清晰化时间）。
- 缓存有总内存上限，按 LRU 淘汰，淘汰后降到低一级的档位。

### 3.4 流式压测

M 个窗口同时按 LLM 的速度追加内容（例如每个窗口每秒 50 个 token），验证：只重新解析尾部、增量布局、交互窗口每帧更新、缓存窗口节流更新、不掉帧。

### 3.5 指标（目标值，需要实测校准）

| 指标 | 目标 |
|---|---|
| 1000 个窗口，缩放扫描（1% ↔ 400%） | 核显 120Hz，帧时间 P99 < 8.3ms |
| 平移和缩放时每帧 CPU | < 1ms（不包括后台线程） |
| 清晰化时间 | < 300ms |
| 晋升延迟 | 布局已缓存时 < 1 帧；冷启动 < 50ms |
| 20 个窗口同时流式输出 | 不掉帧；追加到上屏小于 1 帧 |
| 内存 | 有上限，并且公开；超出时降级而不是崩溃 |

### 3.6 测试方法

- 场景脚本（缩放扫描、平移、快速甩动、跳转、开始流式输出）**用 TS、Python、Lua 各写一遍**，通过语义 API 执行，同时验证三种语言的结果一致。
- **确定性时钟模式**：可以复现，用作 CI 回归门禁。**实时模式**：测量真实的帧时间。
- 每次运行输出报告：帧时间分布、档位分布、缓存命中率、后台吞吐量、硬件和驱动信息。
- **只用公开 API**；数据集和脚本都公开；看 P99，不看平均值。

```ts
// bench/zoom-sweep.ts（示意）
import { canvas, wait, bench } from "openmui";
await canvas.fit("all");
await bench.record("zoom-sweep", async () => {
  for (const z of [0.01, 0.1, 1, 4, 1, 0.1, 0.01]) {
    await canvas.zoom_to(z, { around: "center", duration: 800 });
  }
});
await wait.settled();
bench.report();
```

---

## 4. 运行时需要的能力 → 里程碑

| 能力 | 用在哪里 | 里程碑 |
|---|---|---|
| 富文本引擎（行内样式、断行、双向文字、CJK、回退、选区） | Markdown | M2 |
| 块级布局缓存、增量布局 | 长文档、流式输出 | M2 |
| 任务系统（线程池、优先级、取消） | 后台的解析、布局、光栅化 | M2–M4 |
| 渲染到纹理 / 图层缓存、图集、mipmap、上传预算 | T1 / T2 档位 | M4 |
| 实例化 SDF、msdf 文字、相机 uniform | 画布 | M4 |
| 空间索引、LOD 策略、组件晋升 | 画布 | M4 |
| 语义虚拟化 + 空间查询（`OMUI_EXT_canvas`） | agent 和测试 | M4 |
| 基准测试和遥测接口 | 报告 | M4–M5 |

**提前验证**：M1 期间就做一个最小的 GPU 原型（1 万个矩形加标签，支持平移缩放，带帧时间面板），用实测数据校准 §3.5 的目标。

---

## 5. 示例目录

| # | 示例 | 展示的内容 | 里程碑 |
|---|---|---|---|
| 01 | hello-im | C++ 立即模式，最短路径 | M1 |
| 02 | hello-ts | TS builder + 可选的 `.omui` 视图，不需要写 C++（模板 `openmui:app-ts`） | M3 |
| 03 | counter ×5 | C++、TS、Python、Lua、`.omui` 各写一遍，语义树完全相同 | M3 |
| 04 | custom-component | 契约 → C++ → 三种语言自动可用 | M3 |
| 05 | native-module | `kind=native` | M3 |
| 06 | theming | token、暗色、密度、动效 | M3 |
| 07 | headless-and-agent | H0–H2 测试 + 语义脚本 + 效果分级拦截 | M1 |
| 08 | markdown-viewer | Markdown 组件、扩展块、链接的 external 效果 | M2 |
| 09 | markdown-stream | 模拟 AI 流式输出 | M2 |
| 10 | canvas-basics | 只有核心和默认行为：平移、缩放、LOD | M4 |
| 11 | **markdown-wall** | **旗舰示例：自定义交互 + 上千个 Markdown 窗口 + 基准报告** | M4 |
| 12 | notes-mixed | 多语言混合的完整应用 | M3–M4 |
| 13 | embed-in-imgui | 跨框架桥（互相嵌入） | 以后 |
