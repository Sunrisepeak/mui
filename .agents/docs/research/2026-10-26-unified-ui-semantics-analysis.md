> **历史讨论稿。** 文中的 `openui`、`mui` 是早期的工作名，后来统一改为 **openmui**。凡是和设计文档不一致的地方，以 `.agents/docs/2026-10-26-openmui-*.md` 为准。

# mui：基于 mcpp + C++ Modules + Luban 规格体系的统一 UI 语义层

## 综合分析与架构讨论（2026-10-26）

> 输入：前一份《UI 框架调研报告》，以及本地的 `mcpp`、`mcpp-index`、`mcpp-plugins`、`openkal`、`xlings/luban`（specs / kits / xdesk / os）、`qt-demo`。
> 写法说明：标注了路径的内容是**本地已有事实**；“建议 / 草案 / 示意”是**设计提议**。示意代码和命令只表达设计意图，接口名还没有定。

---

## 0. 结论先行

1. **统一的应该是“语义”，而不是“控件 API”。** 把 Qt、Flutter、ImGui、LVGL、Godot 的 API 合成一套，做不到，也没有必要。真正能统一、也值得统一的，是“界面上有什么、处于什么状态、能对它做什么、做了会有什么后果”。所以建议做**两个东西**：
   - **一份规格**（工作名 `openui`）：语义树、动作、效果和传输协议，与具体框架无关。
   - **一个参考实现工具包**（工作名 `mui`）：用 C++ Modules 写，原生支持立即模式和保留模式，同时覆盖桌面、嵌入式、游戏引擎和 headless 场景。
   其他框架（ImGui/imkit、Qt、Godot、GTK）通过**适配器**接入同一份语义规格。这样即使是用别的框架写的应用，agent 也能用同一种方式控制。

2. **Luban 现有规格体系已经提供了大半底座**，缺的是语义层：

   | 需要的能力 | 已有资产 |
   |---|---|
   | 像素与事件、跨地址空间、串口 / 共享内存、headless 虚拟显示 | `specs/opensal` 0.7、`kits/openhal-emu` |
   | 谁在操作、替谁操作、操作的效果等级（read / reversible / irreversible / external）、需要确认时“退出码 2，绝不等待” | `specs/openas` 0.1 |
   | 对象按路径和接口发布、call / topic / stream / region 等交互、节点自带 actor、授权钩子 | `specs/openbus` 0.2、`services/xbus` |
   | 不同部件之间的能力匹配 | `specs/opencaps` |
   | 内核 ABI，可以跨裸机、Web、UEFI | `openkal` |
   | 模块化构建、交叉编译到 RISC-V / Cortex-M、规则包、打包分发 | `mcpp`、`mcpp-plugins` |
   | 桌面系统本身 | `xdesk`（base / layout / compositor / app） |
   | 立即模式控件、按名字定位控件的测试锚点 | `kits/imkit`（Dear ImGui 1.92.8-docking） |

   **缺的就是一层“语义树规格 + 统一的布局、控件和绘制核心”。**

3. **xlings 已有的原则可以直接搬到 UI 上。** `docs/50-agents-and-machine-interface.md` 里写着：*“Core and surfaces are separate … three renderings of the same result — so they do not drift apart.”* 放到 UI 里就是：**语义树是核心，像素、读屏器、agent 拿到的 JSON、测试断言，都只是同一棵树的不同渲染。**

4. **立即模式和保留模式在核心里是同一种东西**：两者都是“声明出一棵树”。区别只在于立即模式每帧整体重建再做一次对账（reconcile），保留模式只在信号变化时局部重建。对账之后，它们产出的是同一棵语义树和同一份显示列表。

5. **agent 控制建议分三层**：
   - **Verb 层**：应用级命令，用 openas 描述，最稳健。
   - **语义 UI 层**：对节点执行动作，可视、可演示，和人类的操作一致。
   - **像素 / 输入层**：opensal 事件或 xdesk 控制 socket，作为兜底，用于不支持语义的外部应用和视觉校验。

   三层都经过 openas 的效果分级和 openbus 的授权。

---

## 1. 本地资产盘点

### 1.1 构建与生态：mcpp

| 事实 | 出处 | 对 UI 框架的意义 |
|---|---|---|
| C++23 模块优先，`import std` 自动处理，文件级增量，自举构建 | `mcpp/README.md` | 可以写纯模块化的 UI 核心 |
| 在裸机（`os = none`）上 **`import std` 不可用**，要改为 `import mcpplibs.std.freestanding;` | `mcpp/docs/04-mcpp-toml.md §2.7.2`、`40-baremetal.md` | 核心模块必须能在 freestanding 子集上编译 |
| 裸机上**异常和 RTTI 强制关闭，而且这个设置记录在 BMI 里**：带异常编译的依赖不能被无异常的单元导入 | 同上 | **核心代码从源头就要写成无异常、无 RTTI**，错误统一用 `std::expected` |
| `--target` 可以指向 Windows / macOS / Cortex-M / RISC-V，通过 `runner` 交给 QEMU 运行 | 同上，`20-toolchains.md` | 嵌入式 UI 可以在 CI 里跑 |
| `build.mcpp` 和规则包（CUDA / HIP / SYCL / Vulkan-SPIR-V…），`rules-qt` 负责 moc / uic / rcc / lrelease | `mcpp-plugins/docs/rules*.md`、`qt-demo/mcpp.toml` | 可以照这个模式写 `rules-mui`：编译 DSL、烘焙字体和图片、编译着色器 |
| `dist`：apk / apple / web / wix / appimage | `mcpp-plugins/dist/` | 各平台的打包分发已有基础 |
| `[features]` 是 Cargo 风格、可叠加的特性开关 | `06-features-and-capabilities.md` | 可以用特性开关切换 MCU、桌面、引擎等不同配置 |

**mcpp-index 里和 UI 相关的包**（`mcpplibs/mcpp-index/pkgs`）：

| 类别 | 包 |
|---|---|
| 平台与窗口 | `compat.sdl3`（3.4.2，自带 SDL_GPU）、`compat.sdl2`、`compat.glfw` |
| 图形 API | `compat.vulkan*`、`khronos.vulkan-hpp`、`compat.vulkan-memory-allocator`、`compat.spirv-*`、`compat.opengl`、`compat.glad` |
| 绘制、文字、图片 | `compat.pixman`、`compat.freetype`、`compat.harfbuzz`、`compat.fribidi`、`compat.msdfgen`、`compat.nanosvg`、`compat.libpng`、`compat.libwebp`、`freedesktop.cairo`、`gnome.pango*` |
| 现成 UI 库 | `imgui` / `ocornut.imgui`、`compat.ftxui`（终端 UI）、`compat.eui-neo`（保留模式）、`huxerui`（GTK4 栈）、`godotengine.godot-cpp-m`（Godot 的模块化封装） |
| Wayland 栈 | `wlroots`、`freedesktop.wayland*`、`libxkbcommon`、`libinput` |
| 脚本与动态化 | `compat.quickjs-ng`、`compat.wamr`（WebAssembly 运行时）、`compat.lua` |
| 序列化与反射 | `compat.yyjson`、`nlohmann.json`、`compat.reflectcpp`、`neargye.magic_enum` |
| 嵌入式 | `cortex-m-rt`、`riscv-virt-rt`、`aarch64-virt-rt`、`std-freestanding*`、`picolibc` |

**index 里目前还没有 LVGL。**

### 1.2 Luban 规格体系（`openxlings/xlings/luban/specs`）

这些规格有一套统一的写法：

- 规格只依赖标准库。
- 规格之间互不引用，由实现层把它们连起来。
- 版本只会新增内容，不改已有内容；读取方跳过自己不认识的部分。
- 每份规格都附带 conformance 检查。

相关规格的要点：

- **opensal 0.7**：surface 的含义是“给它像素，它返回事件”。
  - 所有值都可以跨地址空间传递（X-capable），不持有指针，也不回调。
  - 接口是可选的、按需协商的：`Text`（preedit/commit，即输入法）、`Clipboard`、`Cursor`、`PointerLock`、`Displays`（镜像 / 扩展）、`Compressed`（用 LZ4 压缩帧，给串口这类窄通道用）、`Region`（共享内存，只传偏移）。
  - 定义了三种边界：**S**（构建时选定）、**L**（加载时绑定）、**X**（跨地址空间）。
- **openas 0.1**：
  - actor 的文本形式是 `agent:planner;via=person:alice`，委托只能追加。
  - 效果分四级：`read` / `reversible` / `irreversible` / `external`。
  - `irreversible` 和 `external` 默认先询问；没人能回答时退出码为 2，并给出能回答这个问题的选项。
  - 运行结果用 NDJSON 信封（envelope）汇报。
- **openbus 0.2**：
  - 节点在路径上提供端点（service / topic / device / stream / region），按接口（`<spec>.<name>@<ver>`）解析。
  - 节点带 actor；有授权钩子、QoS 和路由器自述信息。
  - 约定路径里有 `app/<app-id>/...`。
- **opencaps**：部件之间提供什么、需要什么。
- **openwx**：WuXian 无限平面，以及它的状态文件 `wuxian.json`。

### 1.3 xdesk 与 kits

- `xdesk.draw`：无 GPU、无工具包；用 pixman + FreeType + HarfBuzz 画圆角矩形、图片和文字，有字体回退链（CJK），像素格式是预乘 ARGB32。
- `xdesk.shell`：标题栏、面板、启动器、各种浮层。实现方式是**“图片 + 点击区域”**，手工绘制、手工做命中测试，有自己的 Theme 和中英双语。
- `xdesk.scenes`：壁纸引擎场景，CPU 计算，**由种子决定、结果可复现**，并能响应指针。
- `xdesk.compositor`：wlroots 合成器，输出是一个 opensal surface。
  - 有控制 socket，接受 JSON 输入：`{"input":[...]}`。
  - `xdesk msg state` 输出窗口、工作区和小地图的状态；有 flight recorder 遥测。
  - 在 SDL 离屏驱动下可以**完全 headless 地跑 e2e 测试**（`XdeskSession`）。
- **agent 驱动桌面需要 `desktop-control` 授权**，由主机路由器决定并记录（`docs/luban/91-architecture.md`）。
- `kits/imkit`：基于 Dear ImGui 的组件库。它把宏改成了函数（`imkit.imgui`），并提供 **“named anchors for tests that click a control by name”**，这已经是语义 ID 的雏形。
- `os/window`：Luban OS 应用窗口，SDL3 作为 opensal 的主机侧提供者，加上 Dear ImGui。
- `kits/telemetry`、`kits/i18n`、`kits/cursor`、`kits/openhal-emu`（虚拟显示，画面可输出为 PNG）。

### 1.4 现状缺口

| 缺口 | 后果 |
|---|---|
| 没有语义树规格 | agent 和测试只能靠坐标、`xdesk msg state` 这种临时 JSON、imkit 锚点，三套各不相同 |
| `xdesk.shell` 和 `imkit` 是两套 UI 体系 | 主题、文本、布局和测试方式都各写一遍 |
| 没有统一的布局和控件层 | 每新增一个界面都要手写点击区域 |
| xdesk 只有 CPU 绘制 | 复杂动画、高分屏、游戏场景会碰到性能上限 |
| 没有无障碍桥 | 读屏器不可用，EAA 合规有风险 |
| index 里没有 LVGL，也没有 MCU 绘制方案 | 嵌入式要从零开始 |

---

## 2. 核心判断：两层产品形态

```
                ┌───────────────────────────────────────────┐
                │  openui（规格）：语义树 + 动作 + 效果 + 协议  │  ← 与框架无关，可以有多个实现
                └───────────────────────────────────────────┘
                     ▲            ▲            ▲            ▲
          原生实现    │   适配器    │   适配器    │   适配器    │
        ┌──────────┐  ┌─────────┐  ┌─────────┐  ┌──────────────────┐
        │   mui    │  │ imkit / │  │ Qt      │  │ Godot / GTK /    │
        │（立即+   │  │ ImGui   │  │ QAccess │  │ AT-SPI / Web DOM │
        │  保留）  │  │ 锚点    │  │ -ible   │  │                  │
        └──────────┘  └─────────┘  └─────────┘  └──────────────────┘
```

- 这和 Luban 的规则一致：规格 → kit 实现 → 被其他组件消费。按 `luban/README.md`，规格是“遇到第二个实现者时就可以移出仓库”。适配器天然就是第二、第三个实现者。
- **业界先例**：
  - Flutter 的 `SemanticsNode` 树：所有自绘框架里最成熟的语义层，带 tap、increase、scroll 等动作。
  - AccessKit：Rust 实现的跨平台无障碍树，egui、Slint、Godot 4.5 都在用。
  - Web 的 ARIA / AOM。
  - Android UiAutomator 和 iOS XCUITest，都建立在无障碍树之上。
- **建议** openui 的 role 集合直接对齐 ARIA / AccessKit 的一个子集。这样映射到系统无障碍接口（AT-SPI / UIA / NSAccessibility）几乎零成本。

---

## 3. 总体架构

```
┌────────────────────────────────────────────────────────────────────────────┐
│ L6 消费者：人（像素）│ 读屏器（AT-SPI / UIA / AX）│ Agent（bus / MCP）│ 测试 │
├────────────────────────────────────────────────────────────────────────────┤
│ L5 语义层 openui：Node{id, role, name, value, state, actions[effect, verb]} │ ← 唯一事实来源
│    Verb 注册表（openas 描述；按钮、菜单、快捷键、CLI、agent 共用同一组 verb）│
├──────────────────────────────────┬─────────────────────────────────────────┤
│ L4a 立即模式 mui.im               │ L4b 保留 / 声明式 mui.rt                 │
│  每帧声明 + ID 栈 + 对账           │  signals + 组件树 + .mui DSL             │
├──────────────────────────────────┴─────────────────────────────────────────┤
│ L3 控件 mui.widgets（同一套控件，两种 API 写法）· 主题 token · i18n kit     │
├────────────────────────────────────────────────────────────────────────────┤
│ L2 布局 mui.layout（flex / grid / 栈）· 文本 mui.text（HarfBuzz / FreeType / │
│    fribidi / msdf / 预烘焙位图）· 输入与焦点 · 动画（可注入时钟）            │
├────────────────────────────────────────────────────────────────────────────┤
│ L1 绘制 mui.gfx：显示列表 + 脏区 → 后端                                     │
│    cpu │ gpu（SDL_GPU / Vulkan）│ mcu（局部帧缓冲）│ imgui 桥 │ engine │ remote │
├────────────────────────────────────────────────────────────────────────────┤
│ L0 宿主（opensal surface）：SDL3 窗口 │ xdesk / wlroots │ 虚拟显示 │ 串口（'z'）│
│    共享内存（Region）│ 引擎纹理                                              │
│    openhal 设备 · openkal 内核 ABI · openbus 传输 · openas 身份 · opencaps 能力 │
└────────────────────────────────────────────────────────────────────────────┘
```

**设计约束：**

1. **L5 由 L4 产出，不依赖 L1。** 即使不渲染一个像素，语义树也存在，这正是 headless 和 agent 场景需要的。
2. **L1 以下都是可以替换的后端**，按 opensal 的 S/L/X 边界选择：MCU 用 S（编译期选定，无虚函数）；桌面插件用 L；远程显示和 agent 用 X。
3. **mui 不拥有主循环**，是否由宿主驱动可以配置：`frame(dt, events) -> (tree_delta, display_list, damage)`。这是嵌入游戏引擎和 headless 测试的前提。

---

## 4. 语义层 openui（规格草案 0.1）

### 4.1 节点模型（示意）

```cpp
// specs/openui/src/openui.cppm —— 规格只依赖标准库；MCU 上可用 freestanding 子集实现
export module openui;
import std;                          // 裸机：import mcpplibs.std.freestanding;

export namespace openui {

enum class Role : std::uint8_t {     // 对齐 ARIA / AccessKit 子集
    window, dialog, group, toolbar, button, toggle, checkbox, radio, slider,
    textbox, text, image, list, item, tree, treeitem, tab, tabpanel,
    menu, menuitem, progress, canvas, viewport3d, custom
};

enum class Effect : std::uint8_t { read, reversible, irreversible, external };   // 与 openas §3.1 一致

enum class Origin : std::uint8_t { app, user_content, remote };  // 文本来源，用于防提示注入

struct State {
    bool disabled = false, focused = false, checked = false, selected = false,
         expanded = false, busy = false, hidden = false;
};

struct Action {
    std::string name;        // activate / focus / set_value / increment / scroll_to / ...
    Effect effect = Effect::read;
    std::string verb;        // 触发的应用级 verb（可为空），如 "doc.delete"
    std::string answer;      // irreversible / external 时，能回答询问的选项
};

struct Rect { float x = 0, y = 0, w = 0, h = 0; };

struct Node {
    std::string id;                      // 稳定路径："editor/toolbar/save"
    Role role = Role::group;
    std::string name, description, value;
    Origin origin = Origin::app;
    State state;
    std::vector<Action> actions;
    std::optional<Rect> bounds;          // 逻辑像素；只有语义、没有布局时可以缺省
    std::vector<std::string> children;   // 子节点用 id 引用：值语义，可跨地址空间
};

}  // namespace openui
```

**规格条款要点（草案）：**

1. **ID 稳定**：同一个逻辑控件在多帧、多次会话之间 ID 不变。ID 是路径形式，字符集与 openbus 路径段兼容。
2. **值语义**：所有值都可以跨地址空间（照搬 opensal §3.2）。
3. **动作必须声明效果**：效果为 `irreversible` / `external` 的动作必须声明 `answer`（照搬 openas §3.2、§3.3 和 conformance 6.6）。
4. **Origin**：文本来自用户内容或远端时必须标记。agent 读到这类文本时把它当作**数据，而不是指令**，用来防提示注入。
5. **增量**：树有 revision；变更是有序的 added / removed / updated 序列；读取方可以跳过不认识的字段（照搬 openas §5.3）。
6. **与 verb 绑定**：如果一个动作对应某个应用 verb，就写上 verb 名，agent 可以选择绕过 UI 直接调用 verb。
7. **MCU 上的表示**：规格只定义值的含义和线上格式；实现可以用定长容器（例如 `inplace_vector` 或固定池）。

### 4.2 总线接口：`openui.tree@0.1`

端点路径：`host/<h>/system/<s>/app/<app-id>/ui`（openbus §2.4 的约定）。

| 名称 | openbus 交互类型 | 作用 |
|---|---|---|
| `snapshot` | service（call） | 获取某个 revision 的树。可以只取子树、限制深度，或只取可见 / 可交互节点（节省 LLM 上下文） |
| `changes` | topic | 增量变更流 |
| `find` | service | 按 role、name 通配、状态查找节点 |
| `act` | service | 对节点执行动作。actor 用 openas 的文本形式，返回 openas 信封 |
| `wait` | service | 等待条件成立（节点出现或状态变化），带超时。用它代替 sleep |
| `capture` | stream / region | 可选：返回节点区域的像素，用于视觉校验 |

**交互示例（示意）：**

```text
$ xbus call host/h1/system/dev/app/editor/ui find '{"role":"button","name":"删除*"}'
{"nodes":[{"id":"editor/toolbar/delete","role":"button","name":"删除",
  "actions":[{"name":"activate","effect":"irreversible","verb":"doc.delete","answer":"yes"}]}]}

$ xbus call .../ui act '{"node":"editor/toolbar/delete","action":"activate",
                          "actor":"agent:planner;via=person:alice"}'
{"kind":"error","code":"needs-answer","text":"删除不可撤销","hint":"带上 \"answer\":\"yes\" 再次调用"}
{"kind":"result","exitCode":2,"actor":"agent:planner;via=person:alice"}
```

这和 xlings 的 agent 模式（`--agent` 时永不等待、退出码 2、指出能回答的选项）是**同一套语义**。所以对 agent 来说，“操作 CLI”和“操作 UI”的体验一致。

---

## 5. 双模式：立即模式 + 保留模式

### 5.1 统一原理

```
立即模式：每帧调用 build() → 声明流 → 按 ID 对账影子树 → 标记变化节点 ┐
                                                                    ├→ 增量布局 → 增量显示列表 → 脏区
保留模式：signal 变化 → 标记依赖它的节点 → 局部重建子树 ─────────────┘        └→ openui changes（同一棵树）
```

- **立即模式**是“每帧整树声明 + 对账”。可以借鉴 egui：没有输入、定时器或动画时不调用 `build()`，省电问题就解决了。
- **保留模式**是“按需局部声明”，用细粒度信号（Signals）驱动。
- 两种模式在对账之后是**同一种节点**，所以可以**互相嵌套**：保留模式的组件树里可以放一个立即模式面板（适合调试、属性面板），立即模式的代码里也可以挂一个保留模式组件（适合复杂、带动画的控件）。

### 5.2 立即模式（示意）

```cpp
import mui;

void editor_panel(mui::Im& ui, Doc& doc) {
    ui.window("编辑器", [&] {
        ui.row([&] {
            if (ui.button("保存", {.verb = "doc.save", .effect = mui::Effect::reversible}))
                doc.save();
            if (ui.button("删除", {.verb = "doc.delete", .effect = mui::Effect::irreversible}))
                doc.remove();
        });
        ui.slider("音量", doc.volume, 0, 100);
        ui.text_input("标题", doc.title);
    });
}

// 签名示意：
//   bool button(std::string_view label, ButtonOpts = {},
//               std::source_location at = std::source_location::current());
// ID = ID 栈 + label（也可以用 at 的 file:line:column 消除重名）。
// 不需要 ImGui 的 "##id" 写法，也不需要宏。模块本来就不导出宏，这里正好避开。
```

### 5.3 保留模式（示意）

```cpp
import mui;

auto counter_view() -> mui::View {
    auto count = mui::signal(0);
    return mui::column({
        mui::text([=] { return std::format("点击了 {} 次", count()); }),
        mui::button("加一")
            .verb("counter.increment", mui::Effect::reversible)
            .on_activate([=] { count.update([](int v) { return v + 1; }); }),
    });
}
```

### 5.4 DSL：可选，用 `rules-mui` 编译

```text
// counter.mui —— 发布时编译为 C++ 模块单元（参考 rules-qt 的 uic / moc）；
//               开发期由 mui.rt 解释执行，支持热重载（参考 QML Hot Reload、Slint LivePreview）
component Counter {
    property <int> count: 0;
    Column {
        Text   { text: "点击了 {count} 次"; }
        Button { text: "加一"; verb: "counter.increment"; activated => count += 1; }
    }
}
```

### 5.5 两种模式怎么选

| 场景 | 推荐模式 | 原因 |
|---|---|---|
| 调试面板、引擎编辑器、属性检查器、内部工具 | 立即模式 | 代码最少，状态就在业务数据里 |
| 产品 UI、桌面 shell、HUD、带动画的界面 | 保留模式 / DSL | 便于设计师协作、动画、主题，也省电 |
| MCU | 保留模式（默认） | 不必每帧对账，RAM 和 CPU 开销更可控 |
| headless 和 agent | 两者皆可 | 产出的都是同一棵语义树 |

---

## 6. 绘制层与后端

### 6.1 显示列表 `mui.gfx`

图元包括：矩形 / 圆角矩形 / 路径 / 图片 / 字形串 / 裁剪 / 变换 / 图层（不透明度、模糊）/ 外部纹理（视频、3D 视口、WebView）。每一帧携带脏区。

这里有一个对照：opensal 已经规定“一帧要写明它带了哪块受损矩形”（§4.2），像素格式也是预乘 BGRA。CPU 后端直接产出这种格式，**零转换**。

### 6.2 后端矩阵

| 后端 | 用途 | 依赖（index 里已有） | 边界 |
|---|---|---|---|
| **cpu** | 桌面兜底、headless 金图、xdesk shell | 由 `xdesk.draw` 演进；pixman、freetype、harfbuzz、nanosvg、libpng | S/L |
| **gpu** | 桌面、移动、游戏 | `sdl3`（SDL_GPU：Vulkan / Metal / D3D12）或直接用 Vulkan；msdfgen 做可缩放文字；SPIR-V 规则包编译着色器 | L |
| **mcu** | 局部帧缓冲 + 脏区 + DMA 双缓冲 | `std-freestanding` + 板级包；可选 ThorVG / LVGL 的绘制单元 | S |
| **imgui 桥** | 在 ImGui 宿主里画 mui（输出到 ImDrawList），或在 mui 里嵌 ImGui 面板 | `imgui` 1.92.8-docking（imkit 已经在用） | S/L |
| **engine** | Godot 或自研引擎 | `godotengine.godot-cpp-m` | L |
| **remote** | 把像素经 opensal.wire 或 openbus 送到别处 | `lz4`（opensal 的 `'z'` 帧） | X |

**GPU 选型建议**：先用 **SDL_GPU**。

- 理由：已经依赖 sdl3；它跨 Vulkan / Metal / D3D12；`os/window` 已经基于 SDL3。
- 自研引擎阶段如果需要更细的控制，再在 `mui.gfx` 之下补一个直接用 Vulkan 的后端。显示列表这个接口层不用变。

### 6.3 文本：自绘框架最难的部分，要提前投入

| 平台 | 方案 |
|---|---|
| 桌面 | HarfBuzz 排版 + FreeType 光栅化 + fribidi 处理双向文字 + 字体回退链（`xdesk.draw` 已经实现了回退链） |
| GPU / 游戏 | msdf 图集，任意缩放都清晰 |
| MCU | 构建期由 `rules-mui` 按字符子集烘焙成位图字体（类似 LVGL 的字体转换器） |
| 输入法 | 走 opensal 的 `Text` 接口（preedit / commit），已经定义好了 |

---

## 7. 各场景落地

### 7.1 桌面 + xdesk

| 步骤 | 内容 | 收益 |
|---|---|---|
| ① | `xdesk.shell` 从“图片 + 点击区域”迁到 mui 保留模式控件（标题栏、面板、启动器、浮层） | 点击区域自动成为语义节点；主题、文字、布局复用 |
| ② | 合成器汇总**整个桌面的语义树**：根是 openwx 平面，下面是工作区，再下面是窗口；每个窗口下挂应用自己的子树 | agent 可以在桌面层面按语义操作：启动应用、切换工作区、整理窗口 |
| ③ | 应用子树的来源有三种：mui 应用直接在 bus 上发布 openui；GTK / Qt 应用通过 AT-SPI 适配；都不支持的应用只提供一个窗口节点加像素 | 外部应用也能被统一控制 |
| ④ | agent 走语义 `act` 时，复用已有的 `desktop-control` 授权，再按效果细分（read 和 irreversible 分开授权） | 授权模型不变，只是粒度更细 |
| ⑤ | e2e 测试从坐标改为 `find` + `act` + `wait`，坐标作为兜底，截图保留做视觉校验 | 测试不再因为布局变化而失效 |
| ⑥ | Luban OS 窗口（SDL3 + ImGui + imkit）：**imkit 锚点 → openui 节点**适配器 | P0 阶段就能做的低成本收益 |

### 7.2 嵌入式

- **配置方式**：用 mcpp 的 `[features]` 定义 `profile-mcu`；按 `[target.<triple>]` 选择源文件和板级包（具体写法示意，要以 mcpp 文档为准）。
- **代码约束**：核心模块（openui、mui.gfx、mui.layout、mui.widgets 的子集）在**所有平台上**都写成无异常、无 RTTI、可以不用堆。原因是 mcpp 在裸机上会把“异常关闭”记进 BMI，如果桌面版和 MCU 版写法不同，就会出现两套语义，迟早分叉。
- **存储**：容量参数化。MCU 用固定池，桌面用默认分配器或 `std::pmr`。
- **渲染**：局部帧缓冲（至少 1/10 屏）+ 脏区 + DMA；字体和图片在构建期烘焙；有 2.5D GPU（VG-Lite / NemaGFX / DMA2D）时走对应后端。
- **独有优势：设备 UI 也能被 agent 控制。** MCU 通过 UART / USB 用 opensal.wire 发送 `'z'`（LZ4）帧，再加一个精简的 openui 编码上传语义树。主机上的 agent 和测试就能“看见并操作”设备界面。这是 LVGL、TouchGFX 都没有的能力。
- **关于 LVGL**：
  - 不建议包装 LVGL 的控件，那样会出现两套控件。
  - 建议借鉴它的设计：draw unit、脏区、对象样式。
  - 如果需要芯片 GPU 加速，再把 `compat.lvgl` 打包进 index，只用它的绘制单元层。
- **验证路径**：`mcpp build --target riscv64-none-elf` 加 QEMU runner 进 CI，然后再上一块真实的 RISC-V 或 Cortex-M 板。

### 7.3 Headless：分四级

| 级别 | 运行的内容 | 用途 | 已有基础 |
|---|---|---|---|
| **H0 只有语义** | build + 对账，不布局、不渲染 | 单元测试、agent 预演、服务端生成 UI | — |
| **H1 语义 + 布局** | 有 bounds，可以命中测试，没有像素 | 交互逻辑测试、无障碍检查 | `xdesk.layout` 的“纯逻辑可单测”思路 |
| **H2 CPU 光栅** | 确定性像素输出 → 金图 PNG | 视觉回归 | `openhal-emu` 虚拟显示可以输出 PNG |
| **H3 完整会话** | 虚拟显示 + bus + 输入步骤 | e2e 测试 | xdesk 在 SDL 离屏驱动下跑、`XdeskSession` |

**确定性要求**：时钟可注入；字体固定；动画按步推进（参考 `xdesk.scenes` 的“由种子决定”）。

### 7.4 游戏引擎

**A. 嵌入现有引擎（Godot 先行）**

- 通过 `godot-cpp-m` 写 GDExtension。mui 有两种输出方式：渲染到 Godot 纹理，或通过 RenderingServer 输出 CanvasItem 指令。输入来自 Godot 的 InputEvent。
- 语义树照样发布到 bus 上。这样 QA agent 可以操作游戏菜单，用于自动化回归。
- Unreal / Unity 要通过 C ABI 接入：模块无法直接导出给其他语言，需要一层很薄的 `extern "C"`。

**B. 自研引擎**

- mui 的显示列表作为引擎 render graph 中的一个 pass，与引擎共享 GPU 设备（SDL_GPU 或 Vulkan）。
- **世界空间 UI**：UI 渲染到 RT 再贴到 3D 面上；拾取时用射线换算回 UI 坐标；语义节点的 bounds 也带上 3D 变换。
- 编辑器用立即模式 + docking；游戏 HUD 用保留模式 + 动画。**用 mui 写引擎编辑器**，借鉴 Godot“编辑器用自己的 UI 写”的自举做法。
- 引擎实体也可以发布 openui 节点（role 为 `viewport3d` 或 `custom`），方便 agent 做 playtest。
- **边界**：mui 只做 UI，不承担场景、物理、音频。由宿主驱动帧，引擎拥有主循环。

### 7.5 agent 控制语义

| 层级 | 接口 | 何时使用 | 稳健性 |
|---|---|---|---|
| **Verb** | 应用命令注册表（openas 描述：名称、效果、参数、能回答询问的选项）；可以用 `<app> interface --describe` 列出 | 只要应用提供了 verb，就优先用 | 最高 |
| **语义 UI** | `openui.tree`：find / act / wait / snapshot | 需要“像人一样操作”、需要演示或录制、verb 没覆盖的场景 | 高 |
| **像素 / 输入** | opensal 事件、xdesk 控制 socket 的 `{"input":[...]}`、截图 | 外部应用、视觉校验 | 低 |

**安全与可审计：**

- 授权按 actor 和效果细分：read 自动放行；reversible 可以配置；irreversible 和 external 必须有人回答，或者事先授予权限。
- **防提示注入**：Origin 为 `user_content` 或 `remote` 的文本在 agent 侧一律当作数据。
- **审计**：每次 `act` 产生一个 openas 信封（含 actor、trace），写入 telemetry 的 flight recorder。
- **节省上下文**：snapshot 支持“只取可交互节点、深度限制、增量订阅”。
- **MCP 桥**：一个很小的 `mui-mcp` 程序，把 bus 接口映射为 MCP 工具：`ui_snapshot`、`ui_find`、`ui_act`、`ui_wait`、`ui_capture`。

---

## 8. 各框架的优点如何吸收

| 来源 | 优点 | mui / openui 的对应机制 | 可复用的本地资产 |
|---|---|---|---|
| Flutter | 自绘一致性、Semantics 树、热重载 | `mui.gfx` 自绘 + openui + DSL 解释期热重载 | `xdesk.draw` |
| React Native | 原生无障碍、动态化 | openui → AT-SPI / UIA / AX；可选脚本层 | `quickjs-ng`、`wamr` |
| Qt | 属性绑定、QML、RHI、嵌入式、LTS | signals + `.mui` + `rules-mui`；规格版本只增不改 | `rules-qt` 先例 |
| ImGui | 立即模式工具的开发效率 | `mui.im`；imkit 作为过渡 | `imkit`、`imgui` |
| LVGL | 脏区、局部帧缓冲、绘制单元 | mcu 后端 | mcpp freestanding、`openkal` |
| Tauri | 小体积、能力 ACL | openas 效果分级 + openbus 授权 | openas、openbus |
| MyGo | Web 窗口和原生窗口混用 | opensal 之下所有 surface 地位平等；可选 WebView 外部纹理节点 | opensal |
| Godot | 场景树、编辑器自举、引擎内 UI | engine 后端；用 mui 写编辑器 | `godot-cpp-m` |
| Slint | DSL 编译成原生代码，MCU 到桌面一套代码 | `rules-mui` + 特性配置 | mcpp 规则包 |
| egui | AccessKit + MCP 检查 | `openui.tree` 总线接口 + MCP 桥 | xbus |
| Compose | snapshot state | signals 实现的参考 | — |
| Electron / Web | 生态 | 不复制；提供 WebView 节点 + DOM 无障碍适配器 | — |

---

## 9. C++ Modules 工程策略

1. **包布局**（遵循 `luban/README.md` 的分层规则和 `tools/lint_layer_deps.sh`）：

   | 包 | 内容 |
   |---|---|
   | `luban/specs/openui` | 规格，只依赖标准库，附带 conformance |
   | `luban/kits/mui` | `mui.gfx`、`mui.layout`、`mui.text`、`mui.im`、`mui.rt`、`mui.widgets`，用模块分区（`export module mui; export import :im; ...`） |
   | `luban/kits/mui-backend-{cpu,gpu,mcu,imgui,godot}` | 各后端；C 头文件只出现在 `native/` 绑定模块里（沿用 `tools/lint_c_headers.sh`） |
   | `luban/kits/mui-a11y` | AT-SPI 优先（xdesk 跑在 Linux / Wayland 上），之后补 UIA / AX |

   **注意分层限制**：kit 不能 import service，所以把语义树发布到 bus 的那一层（依赖 `xbus.node`）**不能放进 kit**。可以放在应用层或 xdesk 层，或者让 kit 只依赖 openbus 规格加一个传输抽象。
   如果 mui 以后有了仓库外的用户，按规则整体移出即可。

2. **API 不用宏**：ID 用 `std::source_location` 生成；编译期字符串用 `consteval`。
3. **核心不用异常、不用 RTTI**，错误一律用 `std::expected`（理由见 7.2）。
4. **多态方式与边界对应**：S 边界用 concepts 做静态多态（MCU 上没有虚表）；L 边界用类型擦除的接口；X 边界走线上协议。
5. **反射**：
   - 现阶段用 index 里已有的 `reflectcpp` 做聚合体反射，实现 `ui.form(settings)` 自动生成表单和语义节点。
   - C++26 静态反射（P2996）已经进入标准。等工具链支持稳定后，再用它自动生成表单和 bus schema。
6. **`rules-mui` 规则包**：
   - `.mui` → `.cppm`；
   - 字体和图片烘焙；
   - 着色器编译复用已有的 Vulkan / SPIR-V 规则；
   - i18n 复用 `kits/i18n`。
7. **热重载**：
   - DSL 在开发期解释执行。
   - C++ 逻辑主要依赖 mcpp 的文件级增量构建实现快速重启；可选用 dlopen 重新加载“逻辑模块”。
   - C++ 级别的热补丁成本很高，短期不做。
8. **IDE**：用 mcppls（模块语言服务器）。打包分发复用 `mcpp-plugins/dist`。

---

## 10. 路线图

| 阶段 | 交付内容 | 验收标准 |
|---|---|---|
| **P0 语义规格 + 适配器**（不写新的渲染） | `openui` SPEC 0.1 + conformance；imkit 锚点适配器；`xdesk msg state` → openui 树；bus 接口 `openui.tree@0.1`；`act` 接入 openas 效果分级和退出码 2 | agent 能在 Luban OS 窗口和 xdesk 里按语义点按钮，不可逆操作会被拦下并提示用哪个选项确认 |
| **P1 mui 核心**（桌面 CPU + 立即模式 + headless） | `mui.gfx` + cpu 后端（由 `xdesk.draw` 演进）+ flex 布局 + 文本 + `mui.im` + 基础控件 + H0–H2 测试；宿主走 opensal | 用 `mui.im` 重写一遍 imkit 的控件集，通过金图测试；同一个程序能在 SDL3 窗口、xdesk、虚拟显示上运行 |
| **P2 保留模式 + DSL + GPU** | signals、`mui.rt`、`rules-mui`、解释期热重载、SDL_GPU 后端；`xdesk.shell` 迁移；AT-SPI 桥 | xdesk shell 全部由 mui 驱动，桌面语义树可用，读屏器可用 |
| **P3 嵌入式** | freestanding 配置、mcu 后端、烘焙规则、QEMU 加一块真实板子、经串口远程查看画面并上传语义树 | 同一份控件代码在桌面和 MCU 上都能运行；主机上的 agent 能操作设备 UI |
| **P4 游戏引擎** | Godot GDExtension 后端；世界空间 UI；面向自研引擎的宿主驱动帧接口；用 mui 写编辑器 | 在 Godot 场景里运行 mui HUD，agent 能操作游戏菜单 |

**排序原则**：每个阶段都有现成的使用方（Luban OS 窗口、xdesk、测试），不做没人用的抽象。**P0 不依赖任何新的渲染代码，见效最快。**

---

## 11. 风险与取舍

| 风险 | 说明 | 对策 |
|---|---|---|
| 范围失控 | “吸收所有框架的优点”很容易变成“什么都做、什么都做不完” | 先写规格；每个阶段绑定一个使用方；明确不做什么（见下） |
| 文本、输入法、双向文字、无障碍 | 自绘框架最容易失败的地方 | 复用 HarfBuzz / FreeType / fribidi 和 opensal `Text`；在 P1 就留出测试预算 |
| C++ Modules 工具链 | GCC / Clang / MSVC 行为不一致、BMI 不能跨编译器、编译时间 | 由 mcpp 统一管理；模板放进实现单元；控制接口单元的体积 |
| 双模式的开销 | 立即模式每帧对账要消耗 CPU | 没有事件就不调用 build；MCU 默认用保留模式 |
| agent 安全 | 提示注入、权限过大、误执行不可逆操作 | Origin 标记、按效果分级授权、退出码 2、审计信封 |
| 维护人数 | 单人维护是最大风险（调研报告里对 MyGo 也提过） | 先写规格，接受多个实现，适配器本身就是第二个实现者 |
| 命名冲突 | **MUI** 与 React 的 Material UI（非常知名）重名；**Open UI** 与 W3C Open UI 社区组重名 | 建议正式命名前换掉 |

**明确不做：**

- 不包装其他框架的控件 API，只做语义适配器。
- 不在 UI 层里做场景、物理、音频。
- 短期不做 C++ 热补丁。
- 不追求原生控件观感，走自绘一致性路线；需要原生观感的场景交给适配器接入的原生框架。

---

## 12. 待你决定的问题

1. **命名和归属**：`openui` / `mui` 是否改名？mui 放在 `luban/kits` 下，还是一开始就独立成仓库？
2. **GPU 路线**：先用 SDL_GPU（我的建议），还是直接自建基于 Vulkan 的 RHI？
3. **动态化**：是否需要脚本层（`quickjs-ng` 或 `wamr`）来支持热更新和插件 UI？
4. **优先级**：P3（嵌入式）和 P4（游戏引擎）先做哪个？
5. **role 集合**：是否同意直接对齐 AccessKit / ARIA 子集？（我的建议：对齐）
