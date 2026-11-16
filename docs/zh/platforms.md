# 平台

什么在哪里跑过（发布方案 §2.5）。**实机**：在真实硬件或真实系统服务上跑过。**软件**：在软件实现或虚拟设备上跑过——证明正确，不作性能数字。**未验证**：按契约实现完，自身的测试通过，但这条路径在这里没有可用的运行环境——不算通过，也不算失败。**不支持**：能力查询返回不支持。空格：该平台不提供。

| 能力 | 标注 | Linux x64 (级 1) | Web (wasm32) (级 1) | macOS arm64 (级 1) | Windows x64 (级 1) | Linux arm64 (级 2) | Android (级 2) |
|---|---|---|---|---|---|---|---|
| CPU reference rendering, simulation and the semantic tree | 稳定 | 实机 | 实机 | 实机 | 实机 | 实机 | 软件 |
| 2D on a GPU device (xrender, mlibs.rhi) | 稳定 | 实机 | 未验证 | 软件 | 软件 | 软件 | 未验证 |
| 3D mesh pass on a device (xrender.mesh.gpu) | 预览 | 实机 | 软件 | 软件 | 软件 | 软件 | 未验证 |
| Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu) | 预览 | 实机 | 软件 | 未验证 | 未验证 | 未验证 | 未验证 |
| A window drawn by a device (quay.desktop) | 预览 | 软件 | 未验证 | 未验证 | 软件 | 软件 | 未验证 |
| The Direct3D 12 device backend | 预览 | | | | 未验证 | | |
| The accessibility bridge to the system's screen reader | 稳定 | 实机 | 未验证 | 未验证 | 未验证 | | |
| Input methods (composition, candidates) | 稳定 | 软件 | 软件 | 软件 | 软件 | | |
| Gamepads (Quay, widget breakout) | 预览 | 软件 | 未验证 | 软件 | 软件 | | |
| D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms | 预览 | 未验证 | 未验证 | | | | 未验证 |
| An application's first frame: process start to the window's first pixels (RC3.6) | 稳定 | 未验证 | 未验证 | 未验证 | 未验证 | | |
| Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6) | 稳定 | 实机 | 未验证 | 未验证 | 未验证 | | |
| High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6) | 稳定 | 软件 | 未验证 | 软件 | 软件 | | |
| Closed, slept and resumed: no crash, nothing left blank (RC3.6) | 稳定 | 未验证 | 未验证 | 未验证 | 未验证 | | |

## 每格的依据

- **CPU reference rendering, simulation and the semantic tree**, Linux x64: CI row ubuntu-24.04 (mcpp test --workspace --profile ci)
- **CPU reference rendering, simulation and the semantic tree**, Web (wasm32): CI row wasm32-emscripten under node (omui-devtools check web)
- **CPU reference rendering, simulation and the semantic tree**, macOS arm64: CI row macos-15
- **CPU reference rendering, simulation and the semantic tree**, Windows x64: CI row windows-2022
- **CPU reference rendering, simulation and the semantic tree**, Linux arm64: CI row ubuntu-24.04-arm
- **CPU reference rendering, simulation and the semantic tree**, Android: CI row android x86_64 (emulator; omui-devtools check android)
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux x64: RTX 4080 headless (omui-devtools gpu --real); lavapipe on CI (omui-devtools gpu)
- **2D on a GPU device (xrender, mlibs.rhi)**, Web (wasm32): 缺: which WebGPU adapter omui-devtools check browser's headless Chromium runs on is not recorded; a real browser on a real GPU
- **2D on a GPU device (xrender, mlibs.rhi)**, macOS arm64: CI row macos-15: the runner's Apple Paravirtual Metal device (omui-devtools gpu, cocoa)
- **2D on a GPU device (xrender, mlibs.rhi)**, Windows x64: CI row windows-2022: lavapipe through SDL_GPU's Vulkan backend
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **2D on a GPU device (xrender, mlibs.rhi)**, Android: 缺: a device run; CI builds and runs the emulator without a GPU device case
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux x64: RTX 4080 headless (omui-devtools gpu --real): quay.picture.device; lavapipe on CI
- **3D mesh pass on a device (xrender.mesh.gpu)**, Web (wasm32): Quay's frame on headless Chromium's SwiftShader WebGPU (showcase/quay web_frame_test --frames 20: 46 draws, 8 lights; ~3 s a frame, not performance); 缺: a real browser on a real GPU: web_frame_test through omui-devtools browser --headed under gpu --real where a display is (headless Chromium has no Vulkan on NVIDIA's driver)
- **3D mesh pass on a device (xrender.mesh.gpu)**, macOS arm64: quay.picture.device on CI's Apple Paravirtual device (offscreen)
- **3D mesh pass on a device (xrender.mesh.gpu)**, Windows x64: CI row windows-2022: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Android: 缺: a device run
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Linux x64: mesh.blocks on an RTX 4080 (omui-devtools gpu --real): BC7, 45 of 129,600 pixels past 6 levels, as the same picture as light
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Web (wasm32): rhi.blocks on headless Chromium's WebGPU and WebGL2, all three kinds (omui-devtools check browser); 缺: mesh.blocks in the page's suite at the node; a real browser on a real GPU
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, macOS arm64: 缺: a CI run of mesh.blocks on the runner's Apple Paravirtual device
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Windows x64: 缺: a CI run of mesh.blocks on lavapipe (BC7)
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Linux arm64: 缺: a CI run of mesh.blocks on lavapipe (BC7)
- **Pictures as a device's blocks (KTX2 to BC7, ASTC or ETC2; xrender.mesh.gpu)**, Android: 缺: a device run (ASTC or ETC2)
- **A window drawn by a device (quay.desktop)**, Linux x64: quay.desktop with lavapipe (SDL offscreen video driver)
- **A window drawn by a device (quay.desktop)**, Web (wasm32): 缺: a real browser's WebGPU canvas
- **A window drawn by a device (quay.desktop)**, macOS arm64: 缺: a CI run with RC0.3's fix (the case under cocoa, as the device tests run; red 98)
- **A window drawn by a device (quay.desktop)**, Windows x64: quay.desktop on CI with lavapipe (windows video driver)
- **A window drawn by a device (quay.desktop)**, Linux arm64: quay.desktop on CI with lavapipe
- **A window drawn by a device (quay.desktop)**, Android: 缺: a device run
- **The Direct3D 12 device backend**, Windows x64: 缺: a Direct3D 12 run of the device suites (CI runs Vulkan). Red 11 (the cellular kernel off the CPU on WARP) has a likely root cause fixed: the wrap edge took a remainder of a negative number, undefined on Vulkan and emulated on DXBC; the same fault failed every wrap case on an RTX 4080 (Vulkan), which now passes
- **The accessibility bridge to the system's screen reader**, Linux x64: AT-SPI on CI (omui-devtools check atspi)
- **The accessibility bridge to the system's screen reader**, Web (wasm32): 缺: the page's accessibility tree read by a screen reader
- **The accessibility bridge to the system's screen reader**, macOS arm64: 缺: NSAccessibility read by VoiceOver on a runner that runs it
- **The accessibility bridge to the system's screen reader**, Windows x64: 缺: UI Automation read by a screen reader on a runner that runs it
- **Input methods (composition, candidates)**, Linux x64: synthetic preedit events (omui.core input, omui.ui.base, omui.host.sdl3 tests)
- **Input methods (composition, candidates)**, Web (wasm32): page_test: an input method composing in the hidden textarea, headless Chromium (omui-devtools check browser); 缺: a real IME in a browser
- **Input methods (composition, candidates)**, macOS arm64: synthetic preedit events (the same tests on CI row macos-15)
- **Input methods (composition, candidates)**, Windows x64: synthetic preedit events (the same tests on CI row windows-2022)
- **Gamepads (Quay, widget breakout)**, Linux x64: synthetic gamepad events (xengine.ui ui_test, omui.core input_test, widget-breakout gamepad_test, quay.window's second night with a gamepad)
- **Gamepads (Quay, widget breakout)**, Web (wasm32): 缺: the Gamepad API in a real browser with a pad
- **Gamepads (Quay, widget breakout)**, macOS arm64: the same tests on CI row macos-15
- **Gamepads (Quay, widget breakout)**, Windows x64: the same tests on CI row windows-2022
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Linux x64: 缺: an integrated-GPU machine; only the RTX 4080 is measured
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Web (wasm32): 缺: Quay's frame in a real browser on a real GPU (web_frame_test, omui-devtools browser --headed under gpu --real, where a display is): RC2.1
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Android: 缺: a mobile device
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Linux x64: 缺: process start to the window's first frame, timed; the runtime's part is measured: ui/mount, a 1012-node dashboard built and laid out in 4.4 ms (docs/benchmarks/2026-10-28-desktop)
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Web (wasm32): 缺: the page's first frame timed in a real browser (RC2.9 splits what loads before it)
- **An application's first frame: process start to the window's first pixels (RC3.6)**, macOS arm64: 缺: the same timing on a Mac
- **An application's first frame: process start to the window's first pixels (RC3.6)**, Windows x64: 缺: the same timing on Windows
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Linux x64: site.wall: Tab visits all 36 component cards (and the works) once each, the focus ring drawn at each (CI's site job)
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Web (wasm32): 缺: the same walk in a real browser (the wall's test runs natively, on the Linux site job)
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, macOS arm64: 缺: the wall's test on a macOS row (omui.site runs on the Linux site job only); the library's own Tab cases (omui.ui.base) run there
- **Keyboard reach and visible focus: every component on the wall by Tab, its ring drawn (RC3.6)**, Windows x64: 缺: the wall's test on a Windows row (omui.site runs on the Linux site job only); the library's own Tab cases (omui.ui.base) run there
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Linux x64: omui.host.headless: a ResizeEvent of scale 2 gives the pixels of a window opened at 2x, layout unchanged; the SDL host turns a display-scale change into that event; 缺: two real displays of different scales
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Web (wasm32): 缺: a devicePixelRatio change in a real browser
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, macOS arm64: the same test on CI row macos-15; 缺: a Retina and a non-Retina display
- **High DPI and several displays: moved to a display of another scale, drawn again at it (RC3.6)**, Windows x64: the same test on CI row windows-2022; 缺: two displays of different scales
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Linux x64: 缺: a machine slept and resumed under test; a device lost on resume is let go and made again, else the CPU draws (omui.host.sdl3's desktop host), which no CI row can provoke
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Web (wasm32): 缺: a page hidden and shown, its WebGPU device lost, in a real browser
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, macOS arm64: 缺: a Mac slept and resumed under test
- **Closed, slept and resumed: no crash, nothing left blank (RC3.6)**, Windows x64: 缺: a Windows machine slept and resumed under test

由 `omui-devtools platforms` 从 [release.platforms.xdef.json](../release.platforms.xdef.json) 生成，不要手改。
