<div align="center">

# mui

**Agent-native engines and a UI framework, made to be used one by one or together.**

[![CI](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml/badge.svg?branch=openmui-design)](https://github.com/Sunrisepeak/mui/actions/workflows/ci.yml)
[![openmui](https://img.shields.io/badge/spec-openmui%200.3-blue)](https://github.com/openlifa/openmui)
[![openas](https://img.shields.io/badge/spec-openas%200.2-blue)](https://github.com/openlifa/openas)
[![C++23](https://img.shields.io/badge/C%2B%2B23-modules-informational)](https://github.com/mcpp-community/mcpp)
[![code](https://img.shields.io/badge/code-Apache--2.0-green)](LICENSE)

[中文](README.zh.md) · [**Guide**](docs/en/README.md) · [Features](docs/en/features.md) · [Benchmarks](docs/en/benchmarks/README.md) · [Platforms](docs/en/platforms.md) · [Design](.agents/docs/README.md)

</div>

One repository, several projects. Each project is used on its own — its own README, packages, tools and tests — and any project can leave for a repository of its own. Together they make one system for interfaces, animation, rendering and games, in which people and agents are both first-class users. In development, heading for a first release (0.1).

## Projects

| Project | What it is | Alone | Built on |
|---|---|---|---|
| [**omui**](modules/projects/omui) | A UI framework: one semantic tree drives pixels, motion, accessibility, agents and tests; implements the [openmui](https://github.com/openlifa/openmui) specification | `omui` — new, dev, run, test, package; agents | hanim, mrender |
| [**hanim**](modules/projects/hanim) | A composable, agent-native animation engine: UI animation, video and game animation from one set of capabilities | `hanim` (a document → its frames), `hanim-timeline` | mrender |
| [**mrender**](modules/projects/mrender) | A renderer whose CPU and GPU paths share one source, Web first, its pixels reviewable by agents (code name) | a library | mlibs |
| [**meng**](modules/projects/meng) | A game-engine system: kernels (matter, dynamics, particles, audio, video) and a game layer above omui (code name) | `meng-playtest` | all of the above |
| [**gongfu**](modules/projects/gongfu) | A deterministic, rollback-ready fighting-game framework whose characters are data | — | meng |
| [**mlibs**](modules) | Domain-free libraries beneath every project: math, geometry, images, text, the GPU interface, the agent kernel | libraries | — |

The layers only point down — mlibs ← mrender ← hanim ← omui ← meng's game layer ← works — so taking one project takes only what is beneath it ([AGENTS.md](AGENTS.md) §2–3).

## Works

Each one is a real tool or game, made only from the projects' public parts ([showcase](showcase), [examples](examples)).

| | |
|---|---|
| [Wall](showcase/markdown-wall) · [Styles](showcase/style-gallery) · [Markdown](examples/08-markdown-viewer) · [Notes](examples/12-notes) | Applications in omui |
| [Film](showcase/promo-video) | Sixty seconds rendered from a timeline, every shot a live scene |
| [Swarm](showcase/swarm) · [Breakout](showcase/widget-breakout) · [Fighting](showcase/fighting) | Games an agent plays as a person does |
| [Kiln](showcase/kiln) · [Glade](showcase/glade) · [Quay](showcase/quay) | Worlds of matter (in progress) |

## Start

```sh
omui new hello && cd hello && omui dev          # an application in TypeScript, designed live
omui agent ls                                   # an agent finds it, reads it and acts in it
```

The [guide](docs/en/README.md) goes on per project: interfaces, motion, worlds, games, agents.

## Repository

| | |
|---|---|
| [modules](modules) | mlibs in layers (base, gfx, content) and the projects ([projects](modules/projects)) |
| [tools](tools) | The tools: omui's command line, MCP server and generator, the inspector, and the repository's own (lint, CI) |
| [spec](spec) | The openmui specification, a submodule of [openlifa/openmui](https://github.com/openlifa/openmui) |
| [examples](examples) · [showcase](showcase) · [site](site) | What is made with it |

Built, tested and packaged with [mcpp](https://github.com/mcpp-community/mcpp); the toolchain is pinned in [.xlings.json](.xlings.json) and installed by [xlings](https://github.com/openxlings/xlings). Agents' actors, effects and grants follow [openas](https://github.com/openlifa/openas).

<sub>In development · Code Apache-2.0 · Specification CC BY-SA 4.0</sub>
