# Falling-sand competitors for `cellular.4096.fps` (2026-11-16)

For RC2.18's speed comparison against a Noita-like implementation (the lead metric `cellular.4096.fps`, "a 4096² cellular field on a GPU"). A competitor is run as its own program, pinned, and driven by `omui-devtools bench competitors` (the allowlist's competitor-runtime kind); its licence must allow that and its numbers must be measurable headless on this machine.

## What was found (web search, 2026-11-16)

| Project | Language, structure | Headless / benchmark | Licence | Reported numbers |
|---|---|---|---|---|
| [notita](https://github.com/jhalek90/notita) | C++; 64×64 chunks in a sparse map, passes two chunks apart, a cell reaches at most 32 px outside its chunk; Box2D for bodies | `notita_bench` links only the SDL-free core: 640×384, ~70% of cells powders, liquids, gas; scenes churn, flip, thermal, conduct; 1, 4, 16 threads | none stated | flip with heat: 14.56 ms/tick on 1 thread, 1.84 ms on 16 (Ryzen 7 3800X, Release, 900 ticks) |
| [ron-falling-sand](https://github.com/flykiller13/ron-falling-sand) | C++20, OpenGL 4.3; a CPU branch and a compute-shader branch | none mentioned (GLFW, ImGui window) | none stated | CPU 1000² at 60 fps; GPU 2560×1440 at 60 fps (Ryzen 9 7900X, RTX 4070 SUPER; how measured not said) |
| [Dunefall](https://github.com/arnavsx3/Dunefall) | C++20, Linux, terminal | `--headless`, `--benchmark --width W --height H --steps N`; same seed, same snapshot | none shown | — |
| [dhool](https://github.com/SAMARTHJAISWAL/dhool) | Rust → 36 KB wasm | `examples/bench.rs` | none shown | 0.17 ms a tick |
| [bevy_falling_sand](https://github.com/100-TomatoJuice/bevy_falling_sand) | Rust, Bevy; velocity-based, active chunks only | needs Bevy | MIT / Apache-2.0 | — |
| [rust-sand](https://github.com/nstoddard/rust-sand) | Rust, GLFW | windowed | MIT | — |
| [A Million Pixels of Falling Sand](https://hdyar.com/blog/posts/falling-sand/) | Unity Jobs, chunks a third at a time | — | (article) | 1920×1080 all chunks active at ~30 fps |

Noita's own technique (its GDC talk, [summary](https://dev.to/wlz/why-noita-bet-on-a-custom-falling-everything-engine-the-pixel-simulation-tech-behind-roguelike-1n22)): 64×64 chunks, dirty rectangles, checkerboard passes so chunks updated at once cannot touch. Noita is closed source.

## Where this leaves the comparison

- The structurally closest engine with a headless benchmark (notita) states no licence, and its build is Windows and MSVC only; it cannot be pinned as is. The licensed ones (MIT) are windowed or engine-bound.
- What can be measured now: our 4096² field on the RTX 4080 (`tools`' `gpu --real`), against the published rates above taken as reported (marked so in the ledger: a different machine, not our run), until a licensed headless competitor is pinned — or a clean-room Noita-technique reference (64² chunks, dirty rectangles, checkerboard passes) is written as the allowlist's `reference` kind, its reason stated.
- Asking notita's author for a licence is the shortest way to a like-for-like run.
