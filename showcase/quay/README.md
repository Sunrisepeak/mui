# Quay

A watchkeeper on a harbour at night, in the rain, has to light the beacon at the end of the breakwater before the tide covers it; rusted wardens patrol between the containers, and they find people only by what they hear and see. An original first-person game, M6.4's vertical slice ([the design](../../.agents/docs/2026-11-15-fps-slice-quay-design.md); the plan's §12).

**State of it**: F1 — the simulation — is done and tested; the game as the runtime plays it (an agent player at a person's tier), its picture, its sound and a window are the next checkpoints.

| Package | What |
|---|---|
| [sim](sim) | The harbour as a 3D rigid world (`xengine.dynamics`' rigid3: Jolt under our contract) with the watchkeeper its character; the wardens on the quay's navigation mesh (`xengine.nav`: Detour's crowd) — on their rounds, searching where a noise seemed to come from, chasing what they see and firing; shots that meet the first warden or surface on their way, crates knocked by them; the tide's clock and the beacon. A pure function of the state and the step's actions; the state is a value (both native worlds held by `mlibs::simulation::Branching`): a copy is a branch |

## How the wardens know

A warden never reads where the watchkeeper is. Each step's noises (a shot, running feet a step at a time, a walk's quieter ones) reach it by [`xengine.audio.space`](../../modules/projects/xengine/kernel/audio)'s `reach` — the rule the player's speakers play sound by: the distance, and each surface of the containers on the way letting 0.15 of it by and dulling it above 800 Hz. One loud enough (−58 dB against full) is heard; it turns the warden toward where it seems to have come from — further than it was when steel dulled it, so it walks that way 25 m at most and listens again. It sees within 28 m and 65° of its facing when a ray from its eye to the watchkeeper's head meets nothing on the way.

```sh
mcpp test -p omui.showcase.quay.sim   # the harbour; a copy is a branch; a warden hears a shot through the containers and goes to look, the one 60 m off does not; a player that walks the paths and shoots what it sees lights the beacon at 27.9 s, the same frames every run
```

## Agent surface

| | |
|---|---|
| State | `quay::State`: the watchkeeper, the wardens (each one's mind — patrol, search, chase — what it saw and heard this step), this step's noises and shots, the tide, the outcome and why; `checksum` |
| Control | `quay::Input`: the actions of a step (walk, turn, look, jump, fire, use, run) |
| Time | `step`, and copies: a copy is a branch to play ahead on |
| Not yet | The runtime's subject (`world.observe` at a person's tier: what the watchkeeper sees and hears) — F2 |
