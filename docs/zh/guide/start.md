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
omui new hello                # 默认 TypeScript 模板；--template app-cpp 用 C++；--list 看全部
cd hello
omui dev                      # 开发模式：窗口加设计器，保存即生效
```

`app/main.ts` 就是整个应用：

```ts
import { signal, fmt, column, text, button, mount } from "openmui";

const clicks = signal(0);

mount("main", column({ padding: 24, gap: 12 }, [
  text({ id: "count", text: fmt("Clicked {} times", clicks), style: "heading" }),
  button({ id: "click", text: "Click me", variant: "primary",
           onActivated: () => clicks.update((n: number) => n + 1) }),
]));
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

## 接下来

- [写界面](ui.md)：状态、布局、组件、主题。
- [Agent](agent.md)：让 Claude Code 之类的 Agent 接上你的应用。
