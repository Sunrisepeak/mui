# Platforms

What has been run where (release plan §2.5). **Real**: on real hardware or a real system service. **Software**: on a software implementation or a virtual device — correct, not a performance figure. **Unverified**: implemented to the contract and its own tests pass, but this path has had no environment to run in here — neither a pass nor a failure. **Unsupported**: the capability query says no. A blank cell: not offered on that platform.

| Capability | Label | Linux x64 (tier 1) | Web (wasm32) (tier 1) | macOS arm64 (tier 1) | Windows x64 (tier 1) | Linux arm64 (tier 2) | Android (tier 2) |
|---|---|---|---|---|---|---|---|
| CPU reference rendering, simulation and the semantic tree | stable | real | real | real | real | real | software |
| 2D on a GPU device (xrender, mlibs.rhi) | stable | real | unverified | software | software | software | unverified |
| 3D mesh pass on a device (xrender.mesh.gpu) | preview | real | software | software | software | software | unverified |
| Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu) | preview | real | software | unverified | unverified | unverified | unverified |
| A window drawn by a device (quay.desktop) | preview | software | unverified | unverified | software | software | unverified |
| The Direct3D 12 device backend | preview | | | | unverified | | |
| The accessibility bridge to the system's screen reader | stable | real | unverified | unverified | unverified | | |
| Input methods (composition, candidates) | stable | software | software | software | software | | |
| Gamepads (Quay, widget breakout) | preview | software | unverified | software | software | | |
| D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms | preview | unverified | unverified | | | | unverified |
| An application's first frame: process start to the window's first pixels (RC3.6) | stable | real | unverified | unverified | unverified | | |
| Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6) | stable | unverified | unverified | unverified | unverified | | |
| High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6) | stable | software | unverified | software | software | | |
| Closed, slept and resumed: no crash, nothing left blank (RC3.6) | stable | unverified | unverified | unverified | unverified | | |
| 3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject | preview | real | real | real | real | real | unverified |
| Navigation and crowds (meng.nav) | preview | real | real | real | real | real | unverified |
| Sound in a place (meng.audio.space), rendered on the CPU | preview | real | real | real | real | real | unverified |

## What each cell rests on

- **CPU reference rendering, simulation and the semantic tree**, Linux x64: CI row ubuntu-24.04 (mcpp test --workspace --profile ci)
- **CPU reference rendering, simulation and the semantic tree**, Web (wasm32): CI row wasm32-emscripten under node (omui-devtools check web)
- **CPU reference rendering, simulation and the semantic tree**, macOS arm64: CI row macos-15
- **CPU reference rendering, simulation and the semantic tree**, Windows x64: CI row windows-2022
- **CPU reference rendering, simulation and the semantic tree**, Linux arm64: CI row ubuntu-24.04-arm
- **CPU reference rendering, simulation and the semantic tree**, Android: CI row android x86_64 (emulator; omui-devtools check android)
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux x64: RTX 4080 headless (omui-devtools gpu --real); lavapipe on CI (omui-devtools gpu)
- **2D on a GPU device (xrender, mlibs.rhi)**, Web (wasm32): missing: which WebGPU adapter omui-devtools check browser's headless Chromium runs on is not recorded; a real browser on a real GPU
- **2D on a GPU device (xrender, mlibs.rhi)**, macOS arm64: CI row macos-15: the runner's Apple Paravirtual Metal device (omui-devtools gpu, cocoa)
- **2D on a GPU device (xrender, mlibs.rhi)**, Windows x64: CI row windows-2022: lavapipe through SDL_GPU's Vulkan backend
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **2D on a GPU device (xrender, mlibs.rhi)**, Android: missing: a device run; CI builds and runs the emulator without a GPU device case
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux x64: RTX 4080 headless (omui-devtools gpu --real): quay.picture.device; lavapipe on CI
- **3D mesh pass on a device (xrender.mesh.gpu)**, Web (wasm32): Quay's frame on headless Chromium's SwiftShader WebGPU (showcase/quay web_frame_test --frames 20: 46 draws, 8 lights; ~3 s a frame, not performance); missing: a real browser on a real GPU: web_frame_test through omui-devtools browser --headed under gpu --real where a display is (headless Chromium has no Vulkan on NVIDIA's driver)
- **3D mesh pass on a device (xrender.mesh.gpu)**, macOS arm64: quay.picture.device on CI's Apple Paravirtual device (offscreen)
- **3D mesh pass on a device (xrender.mesh.gpu)**, Windows x64: CI row windows-2022: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Android: missing: a device run
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Linux x64: mesh.blocks on an RTX 4080 (omui-devtools gpu --real): BC7, 45 of 129,600 pixels past 6 levels, as the same picture as light
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Web (wasm32): rhi.blocks on headless Chromium's WebGPU and WebGL2, all three kinds (omui-devtools check browser); missing: mesh.blocks in the page's suite at the node; a real browser on a real GPU
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, macOS arm64: missing: a CI run of mesh.blocks on the runner's Apple Paravirtual device
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Windows x64: missing: a CI run of mesh.blocks on lavapipe (BC7)
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Linux arm64: missing: a CI run of mesh.blocks on lavapipe (BC7)
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Android: missing: a device run (ASTC or ETC2)
- **A window drawn by a device (quay.desktop)**, Linux x64: quay.desktop with lavapipe (SDL offscreen video driver)
- **A window drawn by a device (quay.desktop)**, Web (wasm32): missing: a real browser's WebGPU canvas
- **A window drawn by a device (quay.desktop)**, macOS arm64: missing: a CI run with RC0.3's fix (the case under cocoa, as the device tests run; red 98)
- **A window drawn by a device (quay.desktop)**, Windows x64: quay.desktop on CI with lavapipe (windows video driver)
- **A window drawn by a device (quay.desktop)**, Linux arm64: quay.desktop on CI with lavapipe
- **A window drawn by a device (quay.desktop)**, Android: missing: a device run
- **The Direct3D 12 device backend**, Windows x64: missing: a Direct3D 12 run of the device suites (CI runs Vulkan). Red 11 (the cellular kernel off the CPU on WARP) has a likely root cause fixed: the wrap edge took a remainder of a negative number, undefined on Vulkan and emulated on DXBC; the same fault failed every wrap case on an RTX 4080 (Vulkan), which now passes
- **The accessibility bridge to the system's screen reader**, Linux x64: AT-SPI on CI (omui-devtools check atspi)
- **The accessibility bridge to the system's screen reader**, Web (wasm32): missing: the page's accessibility tree read by a screen reader
- **The accessibility bridge to the system's screen reader**, macOS arm64: missing: NSAccessibility read by VoiceOver on a runner that runs it
- **The accessibility bridge to the system's screen reader**, Windows x64: missing: UI Automation read by a screen reader on a runner that runs it
- **Input methods (composition, candidates)**, Linux x64: synthetic preedit events (omui.core input, omui.ui.base, omui.host.sdl3 tests)
- **Input methods (composition, candidates)**, Web (wasm32): page_test: an input method composing in the hidden textarea, headless Chromium (omui-devtools check browser); missing: a real IME in a browser
- **Input methods (composition, candidates)**, macOS arm64: synthetic preedit events (the same tests on CI row macos-15)
- **Input methods (composition, candidates)**, Windows x64: synthetic preedit events (the same tests on CI row windows-2022)
- **Gamepads (Quay, widget breakout)**, Linux x64: synthetic gamepad events (meng.ui ui_test, omui.core input_test, widget-breakout gamepad_test, quay.window's second night with a gamepad)
- **Gamepads (Quay, widget breakout)**, Web (wasm32): missing: the Gamepad API in a real browser with a pad
- **Gamepads (Quay, widget breakout)**, macOS arm64: the same tests on CI row macos-15
- **Gamepads (Quay, widget breakout)**, Windows x64: the same tests on CI row windows-2022
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Linux x64: missing: an integrated-GPU machine; only the RTX 4080 is measured
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Web (wasm32): missing: Quay's frame in a real browser on a real GPU (web_frame_test, omui-devtools browser --headed under gpu --real, where a display is): RC2.1
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Android: missing: a mobile device
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Linux x64: example 01, release, `--frames 1` (the window asked for to its first frame shown), RTX 4080 under SDL's offscreen driver, 2026-11-17: the CPU renderer 41–53 ms warm (185 ms cold; the whole process 150–270 ms); the GPU renderer 207–334 ms warm (784 ms cold), its device and pipelines made before the first frame. Since then the device is made on a thread of its own from the host's start, beside its fonts, runtime and window: the same dev build interleaved A/B under the same load, 209–328 ms against 403–596 ms before. Not yet held by a CI row
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Web (wasm32): missing: the page's first frame timed in a real browser (RC2.9 splits what loads before it)
- **An application's first frame: process start to the window's first pixels (RC3.6)**, macOS arm64: missing: the same timing on a Mac
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Windows x64: missing: the same timing on Windows
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Linux x64: missing: a Tab walk over every base component in omui.ui.base's tests (the C++ site's wall walk, site.wall, retired with that site on 2026-11-21); the library's own Tab cases run here
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Web (wasm32): missing: the same walk in a real browser
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, macOS arm64: missing: the walk on a macOS row; the library's own Tab cases (omui.ui.base) run there
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Windows x64: missing: the walk on a Windows row; the library's own Tab cases (omui.ui.base) run there
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Linux x64: omui.host.headless: a ResizeEvent of scale 2 gives the pixels of a window opened at 2x, layout unchanged; the SDL host turns a display-scale change into that event; missing: two real displays of different scales
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Web (wasm32): missing: a devicePixelRatio change in a real browser
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, macOS arm64: the same test on CI row macos-15; missing: a Retina and a non-Retina display
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Windows x64: the same test on CI row windows-2022; missing: two displays of different scales
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Linux x64: missing: a machine slept and resumed under test; a device lost on resume ends the application with a message (exit code 3) rather than draw with it — no CI row can provoke a loss — and making it again waits for the runtime to hand components the new device
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Web (wasm32): missing: a page hidden and shown, its WebGPU device lost, in a real browser
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, macOS arm64: missing: a Mac slept and resumed under test
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Windows x64: missing: a Windows machine slept and resumed under test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, Linux x64: CI row ubuntu-24.04, run 44168311060: meng.dynamics rigid3_test, rigid3_parts_test, rigid3_agent_test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, Web (wasm32): CI row wasm32-emscripten under node, run 44168311060: meng.dynamics rigid3_test, rigid3_parts_test, rigid3_agent_test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, macOS arm64: CI row macos-15, run 44168311060: meng.dynamics rigid3_test, rigid3_parts_test, rigid3_agent_test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, Windows x64: CI row windows-2022, run 44168311060: meng.dynamics rigid3_test, rigid3_parts_test, rigid3_agent_test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, Linux arm64: CI row ubuntu-24.04-arm, run 44168311060: meng.dynamics rigid3_test, rigid3_parts_test, rigid3_agent_test
- **3D rigid bodies (meng.dynamics.rigid3, Jolt): stepping, contacts, the agent subject**, Android: missing: the member's tests on the emulator row (it runs example 01 only)
- **Navigation and crowds (meng.nav)**, Linux x64: CI row ubuntu-24.04, run 44168311060: meng.nav.cts nav_test, nav_agent_test
- **Navigation and crowds (meng.nav)**, Web (wasm32): CI row wasm32-emscripten under node, run 44168311060: meng.nav.cts nav_test, nav_agent_test
- **Navigation and crowds (meng.nav)**, macOS arm64: CI row macos-15, run 44168311060: meng.nav.cts nav_test, nav_agent_test
- **Navigation and crowds (meng.nav)**, Windows x64: CI row windows-2022, run 44168311060: meng.nav.cts nav_test, nav_agent_test
- **Navigation and crowds (meng.nav)**, Linux arm64: CI row ubuntu-24.04-arm, run 44168311060: meng.nav.cts nav_test, nav_agent_test
- **Navigation and crowds (meng.nav)**, Android: missing: the member's tests on the emulator row (it runs example 01 only)
- **Sound in a place (meng.audio.space), rendered on the CPU**, Linux x64: CI row ubuntu-24.04, run 44168311060: meng.audio space_test
- **Sound in a place (meng.audio.space), rendered on the CPU**, Web (wasm32): CI row wasm32-emscripten under node, run 44168311060: meng.audio space_test
- **Sound in a place (meng.audio.space), rendered on the CPU**, macOS arm64: CI row macos-15, run 44168311060: meng.audio space_test
- **Sound in a place (meng.audio.space), rendered on the CPU**, Windows x64: CI row windows-2022, run 44168311060: meng.audio space_test
- **Sound in a place (meng.audio.space), rendered on the CPU**, Linux arm64: CI row ubuntu-24.04-arm, run 44168311060: meng.audio space_test
- **Sound in a place (meng.audio.space), rendered on the CPU**, Android: missing: the member's tests on the emulator row (it runs example 01 only)

Generated by `omui-devtools platforms` from [release.platforms.xdef.json](../release.platforms.xdef.json); do not edit.
