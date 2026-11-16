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

## What each cell rests on

- **CPU reference rendering, simulation and the semantic tree**, Linux x64: CI row ubuntu-24.04 (mcpp test --workspace --profile ci)
- **CPU reference rendering, simulation and the semantic tree**, Web (wasm32): CI row wasm32-emscripten under node (tools/ci/web.sh)
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
- **Gamepads (Quay, widget breakout)**, Linux x64: synthetic gamepad events (xengine.ui ui_test, omui.core input_test, widget-breakout gamepad_test, quay.window's second night with a gamepad)
- **Gamepads (Quay, widget breakout)**, Web (wasm32): missing: the Gamepad API in a real browser with a pad
- **Gamepads (Quay, widget breakout)**, macOS arm64: the same tests on CI row macos-15
- **Gamepads (Quay, widget breakout)**, Windows x64: the same tests on CI row windows-2022
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Linux x64: missing: an integrated-GPU machine; only the RTX 4080 is measured
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Web (wasm32): missing: Quay's frame in a real browser on a real GPU (web_frame_test, omui-devtools browser --headed under gpu --real, where a display is): RC2.1
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Android: missing: a mobile device

Generated by `omui-devtools platforms` from [platforms.omui.json](../platforms.omui.json); do not edit.
