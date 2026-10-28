<div align="center">

# openmui

**One semantic tree — rendered as pixels, accessibility, agents and tests.**

[![CI](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml/badge.svg?branch=openmui-design)](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml)
[![openmui](https://img.shields.io/badge/spec-openmui%200.3-blue)](https://github.com/openlifa/openmui)
[![openas](https://img.shields.io/badge/spec-openas%200.2-blue)](https://github.com/openlifa/openas)
[![C++23](https://img.shields.io/badge/C%2B%2B23-modules-informational)](https://github.com/mcpp-community/mcpp)
[![code](https://img.shields.io/badge/code-Apache--2.0-green)](LICENSE)

[中文](README.zh.md) · [Features](docs/en/features.md) · [Benchmarks](docs/en/benchmarks/README.md) · [Design](.agents/docs/README.md)

</div>

An open UI specification and its reference implementation in C++ modules — in development. A view is one tree: the pixels on screen, what a screen reader announces, what an agent reads and acts on, and what a test checks all come from it. The CPU renderer draws the same bytes on Linux, macOS and Windows, with GCC and LLVM; CI holds every pinned image to that.

## Works

Each one is a tool you can use, and each one shows what the framework does.

| | What it is | What it demonstrates |
|---|---|---|
| **[Wall](showcase/markdown-wall)** | A wall of documents on an infinite canvas: a whole folder of Markdown at once | A frame of zooming 1000 windows costs 0.12 ms of CPU at p95 ([benchmarks](docs/en/benchmarks/README.md)); live documents up close, cached textures far away; interactions written as data |
| **[Styles](showcase/style-gallery)** | A settings panel and dashboard in five style packs | Shader effects written in Slang; switching style cross-fades and leaves the semantic tree unchanged |
| **[Markdown](examples/08-markdown-viewer)** | Read documents; [pipe a model's output in](examples/09-markdown-stream) and watch it set as it arrives | CommonMark 652/652, incremental parsing, glyphs fading in |
| **[Space](examples/10-canvas-basics)** | Where a folder's space went, as a map you can zoom | The canvas, a treemap, agents that find by name |
| **[Notes](examples/12-notes)** | Notes written in TypeScript, Lua, a view document and C++ at once | Four languages in one application, sharing state and commands |
| **[Contrast](examples/05-contrast)** | A contrast checker for accessible colours | A native-module contract and its generated bindings |
| **[Inspector](tools/inspector) · [omui](tools/cli) · [MCP](tools/mcp)** | See and drive any running application | A command line and an interface over the same records; agents do only what the person granted |

## Hello

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
omui agent act org.example.hello main/click activate     # an agent presses the same button
```

## Inside

| | |
|---|---|
| [spec](spec) | The specification: a submodule of the standard's own repository, [openlifa/openmui](https://github.com/openlifa/openmui) |
| [modules](modules) | The libraries, in layers: base, gfx, content — each usable alone — and ui, openmui itself |
| [components](components) · [lang](lang) · [tools](tools) | Components, TypeScript / Python / Lua bindings, tools |
| [examples](examples) · [showcase](showcase) | What is made with it |

openmui implements two OpenLifa standards: [openmui](https://github.com/openlifa/openmui) (interfaces) and [openas](https://github.com/openlifa/openas) (actors, effects, grants). It is built, tested and packaged with [mcpp](https://github.com/mcpp-community/mcpp); the toolchain it needs is pinned in [.xlings.json](.xlings.json) and installed by [xlings](https://github.com/openxlings/xlings).

<sub>In development · Code Apache-2.0 · Specification CC BY-SA 4.0</sub>
