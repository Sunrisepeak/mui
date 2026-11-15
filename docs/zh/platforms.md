# 平台

什么在哪里跑过（发布方案 §2.5）。**实机**：在真实硬件或真实系统服务上跑过。**软件**：在软件实现或虚拟设备上跑过——证明正确，不作性能数字。**未验证**：按契约实现完，自身的测试通过，但这条路径在这里没有可用的运行环境——不算通过，也不算失败。**不支持**：能力查询返回不支持。空格：该平台不提供。

| 能力 | 标注 | Linux x64 (级 1) | Web (wasm32) (级 1) | macOS arm64 (级 1) | Windows x64 (级 1) | Linux arm64 (级 2) | Android (级 2) |
|---|---|---|---|---|---|---|---|
| CPU reference rendering, simulation and the semantic tree | 稳定 | 实机 | 实机 | 实机 | 实机 | 实机 | 软件 |
| 2D on a GPU device (xrender, mlibs.rhi) | 稳定 | 实机 | 未验证 | 软件 | 软件 | 软件 | 未验证 |
| 3D mesh pass on a device (xrender.mesh.gpu) | 预览 | 实机 | 未验证 | 软件 | 软件 | 软件 | 未验证 |
| A window drawn by a device (quay.desktop) | 预览 | 软件 | 未验证 | 未验证 | 软件 | 软件 | 未验证 |
| The Direct3D 12 device backend | 预览 | | | | 未验证 | | |
| The accessibility bridge to the system's screen reader | 稳定 | 实机 | 未验证 | 未验证 | 未验证 | | |
| Input methods (composition, candidates) | 稳定 | 软件 | 软件 | 软件 | 软件 | | |
| Gamepads (Quay, widget breakout) | 预览 | 软件 | 未验证 | 软件 | 软件 | | |
| D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms | 预览 | 未验证 | | | | | 未验证 |

## 每格的依据

- **CPU reference rendering, simulation and the semantic tree**, Linux x64: CI row ubuntu-24.04 (mcpp test --workspace --profile ci)
- **CPU reference rendering, simulation and the semantic tree**, Web (wasm32): CI row wasm32-emscripten under node (tools/ci/web.sh)
- **CPU reference rendering, simulation and the semantic tree**, macOS arm64: CI row macos-15
- **CPU reference rendering, simulation and the semantic tree**, Windows x64: CI row windows-2022
- **CPU reference rendering, simulation and the semantic tree**, Linux arm64: CI row ubuntu-24.04-arm
- **CPU reference rendering, simulation and the semantic tree**, Android: CI row android x86_64 (emulator; tools/ci/android.sh)
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux x64: RTX 4080 headless (tools/dev/gpu-real.sh); lavapipe on CI (tools/ci/gpu.sh)
- **2D on a GPU device (xrender, mlibs.rhi)**, Web (wasm32): 缺: which WebGPU adapter tools/ci/browser.sh's headless Chromium runs on is not recorded; a real browser on a real GPU
- **2D on a GPU device (xrender, mlibs.rhi)**, macOS arm64: CI row macos-15: the runner's Apple Paravirtual Metal device (tools/ci/gpu.sh, cocoa)
- **2D on a GPU device (xrender, mlibs.rhi)**, Windows x64: CI row windows-2022: lavapipe through SDL_GPU's Vulkan backend
- **2D on a GPU device (xrender, mlibs.rhi)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **2D on a GPU device (xrender, mlibs.rhi)**, Android: 缺: a device run; CI builds and runs the emulator without a GPU device case
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux x64: RTX 4080 headless (tools/dev/gpu-real.sh): quay.picture.device; lavapipe on CI
- **3D mesh pass on a device (xrender.mesh.gpu)**, Web (wasm32): 缺: a real browser's WebGPU on a real GPU (RC2.1 measures Quay's frame time there)
- **3D mesh pass on a device (xrender.mesh.gpu)**, macOS arm64: quay.picture.device on CI's Apple Paravirtual device (offscreen)
- **3D mesh pass on a device (xrender.mesh.gpu)**, Windows x64: CI row windows-2022: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Linux arm64: CI row ubuntu-24.04-arm: lavapipe
- **3D mesh pass on a device (xrender.mesh.gpu)**, Android: 缺: a device run
- **A window drawn by a device (quay.desktop)**, Linux x64: quay.desktop with lavapipe (SDL offscreen video driver)
- **A window drawn by a device (quay.desktop)**, Web (wasm32): 缺: a real browser's WebGPU canvas
- **A window drawn by a device (quay.desktop)**, macOS arm64: 缺: a CI run with RC0.3's fix (the case under cocoa, as the device tests run; red 98)
- **A window drawn by a device (quay.desktop)**, Windows x64: quay.desktop on CI with lavapipe (windows video driver)
- **A window drawn by a device (quay.desktop)**, Linux arm64: quay.desktop on CI with lavapipe
- **A window drawn by a device (quay.desktop)**, Android: 缺: a device run
- **The Direct3D 12 device backend**, Windows x64: 缺: red 11: the cellular reaction kernel differs from the CPU on WARP; fixed, or Direct3D 12 declared unsupported (RC2.15)
- **The accessibility bridge to the system's screen reader**, Linux x64: AT-SPI on CI (tools/ci/atspi.sh)
- **The accessibility bridge to the system's screen reader**, Web (wasm32): 缺: the page's accessibility tree read by a screen reader
- **The accessibility bridge to the system's screen reader**, macOS arm64: 缺: NSAccessibility read by VoiceOver on a runner that runs it
- **The accessibility bridge to the system's screen reader**, Windows x64: 缺: UI Automation read by a screen reader on a runner that runs it
- **Input methods (composition, candidates)**, Linux x64: synthetic preedit events (omui.core input, omui.ui.base, omui.host.sdl3 tests)
- **Input methods (composition, candidates)**, Web (wasm32): page_test: an input method composing in the hidden textarea, headless Chromium (tools/ci/browser.sh); 缺: a real IME in a browser
- **Input methods (composition, candidates)**, macOS arm64: synthetic preedit events (the same tests on CI row macos-15)
- **Input methods (composition, candidates)**, Windows x64: synthetic preedit events (the same tests on CI row windows-2022)
- **Gamepads (Quay, widget breakout)**, Linux x64: synthetic gamepad events (xengine.ui ui_test, omui.core input_test, widget-breakout gamepad_test)
- **Gamepads (Quay, widget breakout)**, Web (wasm32): 缺: the Gamepad API in a real browser with a pad
- **Gamepads (Quay, widget breakout)**, macOS arm64: the same tests on CI row macos-15
- **Gamepads (Quay, widget breakout)**, Windows x64: the same tests on CI row windows-2022
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Linux x64: 缺: an integrated-GPU machine; only the RTX 4080 and browsers are measured
- **D-187 on weaker machines (integrated GPUs, mobile): p99 ≤ 16.7 ms**, Android: 缺: a mobile device

由 `omui-devtools platforms` 从 [platforms.omui.json](../platforms.omui.json) 生成，不要手改。
