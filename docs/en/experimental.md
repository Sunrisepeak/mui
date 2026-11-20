# Experimental

What ships in 0.1 labelled experimental (D-199): it builds on every tier-1 platform and one headless check shows that it holds, but it is short of its own target, which stands. Nothing stable or preview depends on it. Each entry is its README's Experimental section: the target, where it is now, the gap, its limits and what graduates it to preview.

## omui.reflect

Packages: `omui.reflect` · [README](../../modules/projects/omui/reflect/README.md)

- **Target**: forms and inspectors from C++ types with no contract written, once C++26 reflection is in the toolchains every tier-1 row builds with.
- **Now**: `form(signal)` and its controls; built by GCC 16 with `-freflection` (omui-devtools check reflect), outside the rows' workspace run.
- **Gap**: LLVM has no reflection in the toolchain the rows use; nothing else depends on it.
- **Graduation to preview**: every tier-1 row builds and tests it.

## meng.particles

Packages: `meng.particles` · [README](../../modules/projects/meng/kernel/particles/README.md)

- **Target**: flying matter (sparks, drops, grit) under the standard laws, on the CPU reference and, bit for bit where it holds authority, on the GPU (D-184).
- **Now**: CPU: gravity, drag, collision against triangles, ageing; each particle an instance of matter beside its neighbours and the air.
- **Gap**: no GPU path; magnitudes not checked against hand physics; Glade its one consumer (Quay's fire is xrender.particle's, a picture).
- **Graduation to preview**: a contract suite on every tier-1 row, a second consumer, the magnitudes within tolerance.

## meng.field

Packages: `meng.field` · [README](../../modules/projects/meng/kernel/field/README.md)

- **Target**: the ambient carrier of matter — air, heat, wetness and wind — giving physical magnitudes under the same laws as the other carriers, on the CPU reference and the GPU.
- **Now**: the sparse lattice of 8³ blocks, buoyancy, semi-Lagrangian advection, a fixed count of pressure iterations; Glade its one consumer.
- **Gap**: no radiation (R11b's P1 channel); magnitudes not checked against hand physics; no GPU path.
- **Graduation to preview**: R11's magnitudes within tolerance, a contract suite run on every tier-1 row, a second consumer.

## gongfu

Packages: `gongfu`, `gongfu.data`, `gongfu.present2d`, `gongfu.rules`, `gongfu.studio` · [README](../../modules/projects/gongfu/README.md)

- **Target**: a fighting framework at the level of the best (side by side with Street Fighter 6 on hit feel, readability and rollback), characters as data.
- **Now**: the core is deterministic in fixed point — 10,000 random-depth rollbacks byte-identical; characters compile from data with diagnostics at line and column; six fighters play in `showcase/fighting` from the keyboard or an agent; a match is a game of meng.runtime (`showcase/fighting/headless`), which an agent plays through on a person's tier.
- **Gap**: presentation (supers, hit impact, interface) is behind SF6 in the side-by-side; drawing is 2D only; agents as remote rollback opponents are not done.
- **Limits**: one stage, 2D; no network transport (meng.net has none).
- **Graduation to preview**: two characters play a whole match from the release's packages; an agent plays one at a person's tier; the match's contract runs green on every tier-1 row; the SF6 comparison at par on hit feel and readability.

## xtools

Packages: `xtools` · [README](../../modules/projects/xtools/README.md)

- **Target**: (plan §4.5, RC3.10): the toolchain's whole is one entry and one workbench: each project's toolkit reached the same way alone or composed, and the cross-project tools (the inspector, Live Design's composition of the UI and animation adapters) here as compositions.
- **Now**: the unified command and MCP server over five toolkits; the Inspector composed here; Live Design's composition: the interface's and hanim's design adapters in one workspace under one history, headless (`tests/workspace_test.cpp`, RC3.10's bar), each adapter and the composition passing the adapter's contract (mlibs.agent.sources@1).
- **Gap**: the workbench (an omui application composing the toolkits' panels and the workspace's history); the workspace reached through `xtools` as operations; the interface's live script sources (the player's) as an adapter beside the view documents'.
- **Limits**: nothing of its own beyond the wiring: a toolkit not linked into it is not reachable through it.
- **Graduation to preview**: the workbench and both compositions here, with a headless case that designs a UI and an animation in one workspace (RC3.10's bar).

## xtools.audition

Packages: `xtools.audition` · [README](../../modules/projects/xtools/audition/README.md)

- **Target**: audition any automated property of any track (gain, pan, a filter's cut-off) against the track's own sounds, beside the timeline it came from.
- **Now**: one clip's keyed gain on a real bus under a reference tone; the view's frames, sheet, animation and report; the sound the same at any observation rate.
- **Gap**: one property (gain), a reference tone rather than the track's sounds, whole seconds 1–10.
- **Limits**: a bounded tool, not a resampler or a scene player; sample rates 8–192 kHz.
- **Graduation to preview**: the track's own sounds and any automated property, reached from the timeline editor.

## 格斗游戏 — the fighting game

Packages: `omui.showcase.fighting`, `omui.showcase.fighting.headless` · [README](../../showcase/fighting/README.md)

- **Target**: a fighting game of top independent quality, side by side with Street Fighter 6 (2D against 3D the only allowance).
- **Now**: six fighters, title and select screens, training mode with boxes and a frame meter; gongfu's deterministic rollback core underneath; the fight on its own at the runtime, played through by an agent on a person's tier (`headless`).
- **Gap**: supers, hit impact and the interface fall short of SF6 in the side-by-side; the art pipeline covers Mo fully, the others in part.
- **Graduation to preview**: gongfu's graduation (its README), and the work's own: installed from the release and played for a match (a round is played headless now; the release's packages are RC3's), an agent that wins at a person's tier (one plays through now and loses to the computer), the SF6 table at par.

## Glade

Packages: `omui.showcase.glade.headless` · [README](../../showcase/glade/README.md)

- **Target**: emergence of the class of an open-world sandbox (side by side with a comparable scene of Breath of the Wild, T5.8f): fire, water, wind, ice and lightning answering by the laws, every answer explainable.
- **Now**: the simulation (T5.8a) — terrain, carriers in one state that is a value, the seven verbs as causes; the game at the runtime (T5.8b): an agent reading only the view meets three of the four goals by trying the hand on branches (3 branches), each answer back to the hand (`glade.headless`).
- **Gap**: magnitudes are not yet physical — a struck 56 kg iron rod cools in about 1 s where m·c/(h·A) is about 35 min, a 115 kg block of ice melts in 1.6 s by a campfire (R11: capacity by mass alone, every pair's weight 1, no radiation); burning has no energy balance; an answer that came through the air stops there (the air's own record is not linked into the chain); no picture, sound or window yet (T5.8c–e); the storm goal is not met (a struck rod's heat is R11's).
- **Limits**: one valley; CPU only.
- **Graduation to preview**: T5.8c–e done; the storm goal met; R11's magnitudes within a stated tolerance of hand calculation; one headless check on every tier-1 row.

## Swarm

Packages: `omui.showcase.swarm.headless`, `omui.showcase.swarm.window` · [README](../../showcase/swarm/README.md)

- **Target**: a game an agent makes from one prompt on the engine, as good as one made by hand (T2.8).
- **Now**: the rules, the runtime, the program over lines, and a window that draws the arena (`swarm::picture`) and is played by a person; the one-prompt run took 538 s and 55 turns.
- **Gap**: no sound, one arena, no aiming with a gamepad's right stick (the gun keeps where the pointer last pointed); the prompt not yet run on another engine side by side.
- **Graduation to preview**: sound from the game's events (as Kiln's), the gamepad's aim, and the side-by-side run.

Generated by `omui-devtools docs` from the experimental packages' READMEs; do not edit.
