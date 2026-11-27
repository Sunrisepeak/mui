# 上手

十分钟：装工具、建一个应用、运行、让 Agent 按一下按钮。

## 1. 工具

构建用 [mcpp](https://github.com/mcpp-community/mcpp)，经 xlings 安装；编译器等由工程自己钉住，第一次构建时自动取。

```sh
xlings install mcpp
```

`omui` 命令行目前随仓库构建（项目尚未发布版本）：

```sh
git clone --recursive https://github.com/Sunrisepeak/mui && cd mui
mcpp build -p omui.cli        # 得到 omui，放进 PATH
omui doctor                   # 缺什么工具、各自干什么用
```

## 2. 新建并运行

```sh
omui new hello                # 默认 TypeScript 模板；--template app-cpp 用 C++；`omui templates` 看全部
cd hello
omui dev                      # 开发模式：窗口加设计器，保存即生效
```

`app/main.ts` 就是整个应用（[例子 13](../../../examples/13-live-design) 就从它开始）：

<!-- code: examples/13-live-design/design/start.ts#counter -->
```ts
import { signal, fmt, column, text, button, mount } from "openmui";

const clicks = signal(0);

export const App = () =>
  column({ padding: 24, gap: 12 }, [
    // The page's heading too (role and level): its document's h1 on the Web, a reader's first heading.
    text({ text: "Hello from subscription", style: "title", role: "heading", level: 1 }),
    text({ id: "count", text: fmt("Clicked {} times", clicks), style: "heading" }),
    button({ id: "click", text: "Click me", variant: "primary", onActivated: () => clicks.update((n: number) => n + 1) }),
  ]);

mount("main", App());
```

窗口开着时改这个文件并保存，界面立刻变，计数还在。

## 3. 日常的几条命令

| 命令 | 做什么 |
|---|---|
| `omui dev` | 开发模式运行（可加 `--headless`） |
| `omui run [--release]` | 按发布的样子运行 |
| `omui test` | 跑测试 |
| `omui build [--release]` | 构建 |

它们底下是 mcpp；直接用 `mcpp run`、`mcpp test` 结果相同。

## 4. 让 Agent 按一下

应用在运行时，另开一个终端：

```sh
omui agent ls                                  # 正在运行、可被操作的应用
omui agent snapshot hello --interactive        # 它现在有哪些可操作的东西
omui agent act hello main/click activate       # 按那个按钮
```

窗口里的计数加了一。Agent 用的是和你一样的按钮，不是坐标。

## 5. 按发布的样子：共用运行时上的数据

TypeScript 应用是数据：它的 `mcpp.toml`、`omui.config.ts` 和 `app/`，几 KB。清单在 `[package.metadata.openmui.runtime]` 里写它要运行时提供什么：`openmui = "0.4"`，脚本按哪个规范版本写；`needs = ["agent"]`，用到的能力（`agent`、`generate`、`gpu`、`accessibility-bridge`）；只有必须时才钉一个确切的运行时，`runtime = "=0.1.0"`。模板写好了第一项。

几个版本的运行时并存安装，每个版本一个目录。运行时的包把 `omui-launch` 放上 PATH，它在已装的运行时里选满足声明的最新一个启动应用；有满足的时，`omui run` 也经由它。

```sh
omui-launch --which .         # 会在哪个运行时上启动（runtime@1），或者为什么没有
omui-launch . -- --headless   # 在它上面启动；-- 之后的参数交给播放器
omui doctor                   # 按启动器的读法检查这一节，并说出会用哪个运行时
```

已装的运行时都不满足时，`omui-launch` 说出每一个不满足的原因和装一个的命令（`xlings install omui-runtime@0.1`），退出码 2。

共用运行时要求机器上有 xlings：它服务开发者、工具和同一台机器上的多个 omui 应用。面向普通用户的应用、应用商店和 Web 用自带运行时（`omui package`、`omui build --web`）。现在还没有发布，所以运行时也还没有发布出去。

## 出了问题怎么办

- **缺工具**：`omui doctor` 逐个列出缺少的工具和它的用途；`omui tools install` 经 xlings 装上工程点名的工具。
- **第一次构建很慢**：工程钉住的编译器和库第一次要下载并构建一遍；之后的构建是增量的。
- **`omui agent ls` 里没有这个应用**：应用运行时才列出，靠的是每个用户的运行时目录里的描述文件（`$XDG_RUNTIME_DIR/openmui`，没有就用临时目录）。崩溃留下的也会列出；连上去才知道哪个还活着。
- **要更多细节**：`OMUI_LOG`（或 `--log`）按类别设日志级别，例如 `OMUI_LOG=agent=debug`。

## 接下来

- [写界面](ui.md)：状态、布局、组件、主题。
- [Agent](agent.md)：让 Claude Code 之类的 Agent 接上你的应用。
