<div align="center">

# openmui

**一棵语义树，同时是像素、无障碍、agent 和测试。**

[![CI](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml/badge.svg?branch=openmui-design)](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml)
[![openmui](https://img.shields.io/badge/spec-openmui%200.3-blue)](https://github.com/openlifa/openmui)
[![openas](https://img.shields.io/badge/spec-openas%200.2-blue)](https://github.com/openlifa/openas)
[![C++23](https://img.shields.io/badge/C%2B%2B23-modules-informational)](https://github.com/mcpp-community/mcpp)
[![code](https://img.shields.io/badge/code-Apache--2.0-green)](LICENSE)

[English](README.md) · [特性](docs/zh/features.md) · [最佳实践](docs/zh/best-practices.md) · [基准](docs/zh/benchmarks/README.md) · [设计](.agents/docs/README.md)

</div>

一套开放的 UI 规范，和它用 C++ Modules 写的参考实现（实现中）。一个视图就是一棵树：屏幕上的像素、读屏软件念出的内容、agent 读到和操作的对象、测试检查的结果，都来自这棵树。CPU 渲染器在 Linux、macOS、Windows 上，用 GCC 和 LLVM，画出的字节完全相同；CI 用固定的金图检查每一处。

## 作品

每一个都是能用的工具，也都展示框架的一项能力。

| | 是什么 | 展示了什么 |
|---|---|---|
| **[文档墙](showcase/markdown-wall)** | 无限画布上的文档墙：一次看完整个文件夹的 Markdown | 缩放 1000 个窗口时一帧的 CPU 开销 p95 0.12 ms（[基准](docs/zh/benchmarks/README.md)）；近处是实时文档，远处是缓存的纹理；交互用数据描述 |
| **[风格工坊](showcase/style-gallery)** | 五种风格包下的设置面板和仪表盘 | 用 Slang 写的着色器效果；切换风格时交叉淡化，语义树不变 |
| **[Markdown](examples/08-markdown-viewer)** | 阅读文档；把模型的输出[管道进来](examples/09-markdown-stream)，边收边排版 | CommonMark 652/652、增量解析、逐字淡入 |
| **[空间地图](examples/10-canvas-basics)** | 一个文件夹的空间去了哪里，一张能缩放的地图 | 画布、treemap、agent 按名字查找 |
| **[笔记](examples/12-notes)** | 用 TypeScript、Lua、视图文档和 C++ 一起写成的笔记 | 一个应用里四种语言，共享状态和命令 |
| **[对比度](examples/05-contrast)** | 检查无障碍颜色对比度的工具 | native 模块的契约和生成的绑定 |
| **[Inspector](tools/inspector) · [omui](tools/cli) · [MCP](tools/mcp)** | 查看和操作任何运行中的应用 | 命令行和图形界面读同一份记录；agent 只能做人授权的事 |

## 上手

```cpp
import std;
import openmui;

omui::AppSpec app() {
    static int clicks = 0;
    return {.id = "org.example.hello", .title = "Hello", .view = [] {
        return omui::immediate([](omui::Ui& ui) {
            if (ui.button(std::format("Clicked {} times", clicks), {.id = "click"})) ++clicks;
        });
    }};
}
```

```sh
mcpp new hello --template openmui && cd hello && mcpp run -- --serve
omui agent act org.example.hello main/click activate     # agent 按下同一个按钮
```

## 目录

| | |
|---|---|
| [spec](spec) | 规范：标准自己的仓库 [openlifa/openmui](https://github.com/openlifa/openmui) 的子模块 |
| [modules](modules) | 分层的库：base、gfx、content（都能单独使用），以及 ui（openmui 本身） |
| [components](components) · [lang](lang) · [tools](tools) | 组件、TypeScript / Python / Lua 绑定、工具 |
| [examples](examples) · [showcase](showcase) | 用它做出来的东西 |

openmui 实现两个 OpenLifa 标准：[openmui](https://github.com/openlifa/openmui)（界面）和 [openas](https://github.com/openlifa/openas)（actor、效果、授权）。构建、测试、打包都使用 [mcpp](https://github.com/mcpp-community/mcpp)；需要的工具链版本固定在 [.xlings.json](.xlings.json) 里，由 [xlings](https://github.com/openxlings/xlings) 安装。

<sub>实现中 · 代码 Apache-2.0 · 规范 CC BY-SA 4.0</sub>
