<div align="center">

# mui

**Agent 原生的引擎与 UI 框架：可以单独用，也可以组合起来用。**

[![CI](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml/badge.svg?branch=openmui-design)](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml)
[![openmui](https://img.shields.io/badge/spec-openmui%200.3-blue)](https://github.com/openlifa/openmui)
[![openas](https://img.shields.io/badge/spec-openas%200.2-blue)](https://github.com/openlifa/openas)
[![C++23](https://img.shields.io/badge/C%2B%2B23-modules-informational)](https://github.com/mcpp-community/mcpp)
[![code](https://img.shields.io/badge/code-Apache--2.0-green)](LICENSE)

[English](README.md) · [**指南**](docs/zh/README.md) · [特性](docs/zh/features.md) · [基准](docs/zh/benchmarks/README.md) · [平台](docs/zh/platforms.md) · [设计](.agents/docs/README.md)

</div>

一个仓库，多个项目。每个项目都能单独使用，有自己的 README、包、工具和测试，随时可以拆成独立仓库；组合起来，是一套覆盖界面、动画、渲染和游戏的体系，人和 Agent 都是一等用户。实现中，目标是第一个发布版本（0.1）。

## 项目

| 项目 | 是什么 | 单独使用 | 建在 |
|---|---|---|---|
| [**omui**](modules/projects/omui) | UI 框架：一棵语义树同时驱动像素、动画、无障碍、Agent 和测试；实现 [openmui](https://github.com/openlifa/openmui) 规范 | `omui`：new、dev、run、test、package，Agent 接入 | hanim、mrender |
| [**hanim**](modules/projects/hanim) | 可组合、Agent 原生的动画引擎：UI 动画、视频、游戏动画用同一套能力 | `hanim`（文档 → 帧）、`hanim-timeline` | mrender |
| [**mrender**](modules/projects/mrender) | CPU 与 GPU 同源、Web 优先、像素可供 Agent 审查的渲染引擎（代号） | 库 | mlibs |
| [**meng**](modules/projects/meng) | 游戏引擎系统：引擎内核（物质、动力学、粒子、音频、视频）与 omui 之上的游戏层（代号） | `meng-playtest` | 以上全部 |
| [**gongfu**](modules/projects/gongfu) | 确定性、可回滚、角色即数据的格斗游戏框架 | — | meng |
| [**mlibs**](modules) | 所有项目之下与领域无关的库：数学、几何、图像、文字、GPU 接口、Agent 内核 | 库 | — |

依赖只向下：mlibs ← mrender ← hanim ← omui ← meng 的游戏层 ← 作品。取走一个项目，只需要带上它下面的部分（[AGENTS.md](AGENTS.md) §2–3）。

## 作品

每一个都是真实可用的工具或游戏，只用各项目的公开能力做成（[showcase](showcase)、[examples](examples)）。

| | |
|---|---|
| [文档墙](showcase/markdown-wall) · [风格工坊](showcase/style-gallery) · [Markdown](examples/08-markdown-viewer) · [笔记](examples/12-notes) | 用 omui 做的应用 |
| [宣传片](showcase/promo-video) | 从时间线渲染的六十秒短片，每个镜头都是实时场景 |
| [Swarm](showcase/swarm) · [组件打砖块](showcase/widget-breakout) · [格斗](showcase/fighting) | Agent 能像人一样玩的游戏 |
| [Kiln](showcase/kiln) · [Glade](showcase/glade) · [Quay](showcase/quay) | 物质构成的世界（实现中） |

## 上手

```sh
omui new hello && cd hello && omui dev          # 一个 TypeScript 应用，边运行边设计
omui agent ls                                   # Agent 找到它、读取它、在其中操作
```

[指南](docs/zh/README.md)按项目继续：界面、动画、世界、游戏、Agent。

## 仓库

| | |
|---|---|
| [modules](modules) | 分层的 mlibs（base、gfx、content）与各项目（[projects](modules/projects)） |
| [tools](tools) | 工具：omui 的命令行、MCP 服务与生成器、inspector，以及仓库自己的（lint、CI） |
| [spec](spec) | openmui 规范，[openlifa/openmui](https://github.com/openlifa/openmui) 的子模块 |
| [examples](examples) · [showcase](showcase) · [site](site) | 用它做出来的东西 |

构建、测试、打包都使用 [mcpp](https://github.com/mcpp-community/mcpp)；工具链版本固定在 [.xlings.json](.xlings.json)，由 [xlings](https://github.com/openxlings/xlings) 安装。Agent 的 actor、效果与授权以 [openas](https://github.com/openlifa/openas) 为准。

<sub>实现中 · 代码 Apache-2.0 · 规范 CC BY-SA 4.0</sub>
