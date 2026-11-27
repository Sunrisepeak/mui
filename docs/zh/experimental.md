# 实验

0.1 里标为实验、随发布提供的部分（D-199）：一级平台都能构建，一条无头验证说明它成立，但离它自己的目标还有差距，目标不降。稳定与预览的包不依赖它。每一条是该模块 README 的 Experimental 一节（英文原文）：目标、现状、差距、局限，以及到预览的毕业条件。

## omui.reflect

包：`omui.reflect` · [README](../../modules/projects/omui/reflect/README.md)

- **目标**：forms and inspectors from C++ types with no contract written, once C++26 reflection is in the toolchains every tier-1 row builds with.
- **现状**：`form(signal)` and its controls, `to_value` and `from_value`, `fields<T>()`, verified in standalone graphs only. Inside openmui's graph GCC 16.1 does not build it (V-08, above); `omui-devtools check reflect` builds the smallest graph with the conflict, warns while it fails, and runs the suite once it builds.
- **差距**：the basic gate itself, for a reason outside the project: no tier-1 toolchain builds C++26 reflection beside C headers (GCC 16.1: V-08; Clang 23 has no `-freflection`), so it builds on no row and its one headless check, the suite, waits; nothing else depends on it.
- **局限**：a form edits strings, bools, numbers, enums and structs of them (lists and optionals travel through `to_value`, no control edits them); its graph is built apart, in C++26.
- **毕业条件**：every tier-1 row builds and tests it.

## meng.particles

包：`meng.particles` · [README](../../modules/projects/meng/kernel/particles/README.md)

- **目标**：flying matter (sparks, drops, grit) under the standard laws, on the CPU reference and, bit for bit where it holds authority, on the GPU (D-184).
- **现状**：CPU: gravity, drag, collision against triangles, ageing; touching by position-based dynamics (sand heaps, water runs flat, oil comes up through water); each particle an instance of matter beside its neighbours and the air; an agent's subject under the matter contract's suite.
- **差距**：no GPU path; magnitudes not checked against hand physics; Glade the one work that uses it, beside meng.dynamics' `rigid3-scene` (Quay's fire is mrender.particle's, a picture).
- **局限**：one thread on the CPU; no curl noise or force fields; touching steps in substeps of at most a quarter of the smallest size.
- **毕业条件**：a contract suite on every tier-1 row, a second consumer, the magnitudes within tolerance.

## meng.field

包：`meng.field` · [README](../../modules/projects/meng/kernel/field/README.md)

- **目标**：the ambient carrier of matter — air, heat, wetness and wind — giving physical magnitudes under the same laws as the other carriers, on the CPU reference and the GPU.
- **现状**：the sparse lattice of 8³ blocks, buoyancy, semi-Lagrangian advection, a fixed count of pressure iterations; Glade the one work that uses it, beside meng.dynamics' `rigid3-matter`.
- **差距**：no radiation (R11b's P1 channel); magnitudes not checked against hand physics; no GPU path.
- **局限**：a coarse grid (1 m cells); bodies do not block the wind; a cell is one instance of matter (its heat capacity not by its volume); the CPU only.
- **毕业条件**：R11's magnitudes within tolerance, a contract suite run on every tier-1 row, a second consumer.

## gongfu

包：`gongfu`, `gongfu.data`, `gongfu.present2d`, `gongfu.rules`, `gongfu.studio` · [README](../../modules/projects/gongfu/README.md)

- **目标**：a fighting framework at the level of the best (side by side with Street Fighter 6 on hit feel, readability and rollback), characters as data.
- **现状**：the core is deterministic in fixed point — 10,000 random-depth rollbacks byte-identical; characters compile from data with diagnostics at line and column; seven fighters play in `showcase/fighting` from the keyboard, a gamepad or an agent; a match is a game of meng.runtime (`showcase/fighting/headless`), which an agent plays through on a person's tier.
- **差距**：presentation (supers, hit impact, interface) is behind SF6 in the side-by-side; drawing is 2D only; agents as remote rollback opponents are not done.
- **局限**：one playfield — a flat floor between two walls (`stage_width`), the work's six stages pictures over it — and 2D; no network transport (meng.net has none).
- **毕业条件**：two characters play a whole match from the release's packages; an agent plays one at a person's tier; the match's contract runs green on every tier-1 row; the SF6 comparison at par on hit feel and readability.

## xtools

包：`xtools` · [README](../../modules/projects/xtools/README.md)

- **目标**：(plan §4.5, RC3.10): the toolchain's whole is one entry and one workbench: each project's toolkit reached the same way alone or composed, and the cross-project tools (the inspector, Live Design's composition of the UI and animation adapters) here as compositions.
- **现状**：the unified command and MCP server over six toolkits; the Inspector composed here; Live Design's composition: the interface's and hanim's design adapters in one workspace under one history, headless (`tests/workspace_test.cpp`, RC3.10's bar), each adapter and the composition passing the adapter's contract (mlibs.agent.sources@1).
- **差距**：the workbench (an omui application composing the toolkits' panels and the workspace's history); the workspace reached through `xtools` as operations; the interface's live script sources (the player's) as an adapter beside the view documents'.
- **局限**：nothing of its own beyond the wiring: a toolkit not linked into it is not reachable through it.
- **毕业条件**：the workbench and both compositions here, with a headless case that designs a UI and an animation in one workspace (RC3.10's bar).

## xtools.audition

包：`xtools.audition` · [README](../../modules/projects/xtools/audition/README.md)

- **目标**：audition any automated property of any track (gain, pan, a filter's cut-off) against the track's own sounds, beside the timeline it came from.
- **现状**：one clip's keyed gain on a real bus under a reference tone; the view's frames, sheet, animation and report; the sound the same at any observation rate.
- **差距**：one property (gain), a reference tone rather than the track's sounds, whole seconds 1–10.
- **局限**：a bounded tool, not a resampler or a scene player; sample rates 8–192 kHz.
- **毕业条件**：the track's own sounds and any automated property, reached from the timeline editor.

## 格斗游戏 — the fighting game

包：`omui.showcase.fighting`, `omui.showcase.fighting.headless` · [README](../../showcase/fighting/README.md)

- **目标**：a fighting game of top independent quality, side by side with Street Fighter 6 (2D against 3D the only allowance).
- **现状**：seven fighters, title and select screens, six home stages, training mode with boxes and a frame meter; gongfu's deterministic rollback core underneath; the fight on its own at the runtime, played through by an agent on a person's tier (`headless`).
- **差距**：supers, hit impact and the interface fall short of SF6 in the side-by-side; the art pipeline covers Mo fully, the others in part.
- **局限**：gongfu's (one playfield, 2D; no play over a network); from the keyboard, a gamepad or an agent, two at one machine.
- **毕业条件**：gongfu's graduation (its README), and the work's own: installed from the release and played for a match (a round is played headless now; the release's packages are RC3's), an agent that wins at a person's tier (one plays through now and loses to the computer), the SF6 table at par.

## Glade

包：`omui.showcase.glade.headless` · [README](../../showcase/glade/README.md)

- **目标**：emergence of the class of an open-world sandbox (side by side with a comparable scene of Breath of the Wild, T5.8f): fire, water, wind, ice and lightning answering by the laws, every answer explainable.
- **现状**：the simulation (T5.8a) — terrain, carriers in one state that is a value, the seven verbs as causes; the game at the runtime (T5.8b): an agent reading only the view meets three of the four goals by trying the hand on branches (3 branches), each answer back to the hand (`glade.headless`).
- **差距**：magnitudes are not yet physical — a struck 56 kg iron rod cools in about 1 s where m·c/(h·A) is about 35 min, a 115 kg block of ice melts in 1.6 s by a campfire (R11: capacity by mass alone, every pair's weight 1, no radiation); burning has no energy balance; an answer that came through the air stops there (the air's own record is not linked into the chain); no picture, sound or window yet (T5.8c–e); the storm goal is not met (a struck rod's heat is R11's).
- **局限**：one valley; CPU only.
- **毕业条件**：T5.8c–e done; the storm goal met; R11's magnitudes within a stated tolerance of hand calculation; one headless check on every tier-1 row.

## Swarm

包：`omui.showcase.swarm.headless`, `omui.showcase.swarm.window` · [README](../../showcase/swarm/README.md)

- **目标**：a game an agent makes from one prompt on the engine, as good as one made by hand (T2.8).
- **现状**：the rules, the runtime, the program over lines, and a window that draws the arena (`swarm::picture`) and is played by a person; the one-prompt run took 538 s and 55 turns.
- **差距**：no sound, one arena, no aiming with a gamepad's right stick (the gun keeps where the pointer last pointed); the prompt not yet run on another engine side by side.
- **局限**：one player; the arena drawn on the CPU from the state each frame.
- **毕业条件**：sound from the game's events (as Kiln's), the gamepad's aim, and the side-by-side run.

由 `omui-devtools docs` 从实验包的 README 生成，不要手改。
