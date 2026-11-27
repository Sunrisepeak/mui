# ESP32 工具链 spike（W9，C1）

2026-11-01。[总体方案](2026-11-01-openmui-master-plan-design.md) C1 的 W9 项：用 C++23 模块和 mcpp 交叉构建，编译、链接，并在开发板上跑一个空运行时。Q-35 定的是先做 ESP32-S3（Xtensa LX7），不行时换 ESP32-P4（RISC-V），这是 spike 的备选。

## 1. 结果

| 项 | 结果 |
|---|---|
| ESP32-S3（Xtensa） | **没有工具链**：这里的 LLVM 23.1.3 没有编进实验性的 Xtensa 后端（`clang --print-targets` 里没有 xtensa）；xlings 和 mcpp 也没有 Espressif 的 GCC（xtensa-esp-elf）或 esp-clang |
| ESP32-P4 的指令集（rv32） | **能编译、链接、运行**：mcpp 的 `riscv32-none-elf` 目标（LLVM 23，picolibc）加板级包 `riscv-virt-rt`，在 QEMU 的 RISC-V `virt` 机器上运行。发布版代码 10 KiB，FNV-1a 的结果和主机逐位相同（见 §3） |
| openmui 的模块 | **不能直接用**：裸机目标上 mcpp 只提供标准库的独立实现子集（`mcpplibs.std.freestanding`），没有 `import std;`；openmui 的模块都要用完整的标准库（`string`、`vector`、`map`、`function`、`format`） |
| `std-freestanding` 包 | **发现两个问题**：(1) 0.2.0 到最新的 0.5.2 用 LLVM 23 都编不过，因为 libc++ 23 把 `__libcpp_verbose_abort` 声明在显式 ABI 注解里（补丁见附录）；(2) 对 `int` 调用 `ranges::sort`，链接时缺少 libc++ 已编译好的 `__sort` 实例化 |
| 开发板 | 这台机器上没有板子，用 QEMU 代替 |

## 2. 结论和建议

1. **按 Q-35 的备选，mcu 配置档先做 ESP32-P4（RISC-V）**。ESP32-S3 要等生态里有 Xtensa 工具链（xlings 收录 Espressif 的 GCC 或 esp-clang，或者 LLVM 打开 Xtensa 实验后端）。按“第一阶段不修改 xlings”的约定，这一步不在本仓库做。
2. **mcu 配置档的关键缺口是完整的标准库**。三条路：
   - **(a) 在 picolibc 上构建 libc++**（推荐）：像 LLVM Embedded Toolchain for Arm 那样，不带线程和文件系统，`import std;` 的其余部分可用，openmui 一份代码。需要在 mcpp 生态里加一个包，上游先做好、发布。
   - **(b) ESP-IDF 自带的 GCC**（newlib + libstdc++，带 FreeRTOS，完整标准库）：需要 GCC ≥ 15 才有 `import std`，还没确认 Espressif 的版本。
   - **(c) 用独立实现子集单独写一个“mcu 核心”**（固定容量的容器，没有 `format`）：要维护两份核心，不推荐。
3. **C3 的 mcu 宿主按 (a) 做**：
   - 先做 `openmui.host.esp32` 的 QEMU 版（`virt` 机器上的帧缓冲 + 局部重绘）；
   - 再换到 P4 的屏幕和触摸（MIPI-DSI，通过 ESP-IDF 的 C 接口，经 mwrap）。
4. **上游**：`std-freestanding` 的两个问题提交到 mcpplibs/std-freestanding（附录的补丁，和 `__sort` 等实例化的补充），发布后再在本仓库使用。

## 3. spike 程序

```cpp
import mcpplibs.std.freestanding;
import mcpplibs.riscv_virt_rt;

constexpr std::uint64_t fnv1a(std::string_view s) { … }  // 和 openmui.hash 相同

extern "C" int main() {  // 独立实现下 main 不特殊：要写成 extern "C"
    std::array<int, 4> a{3, 1, 2, 0};
    std::ranges::reverse(a);
    board::printf("openmui spike on riscv32: reversed %d %d %d %d, fnv1a(openmui) %08x%08x\n", …);
    …
}
```

`mcpp run --target riscv32-none-elf`（依赖 `std-freestanding`、`riscv-virt-rt = "0.7.1"`）的输出：

```text
openmui spike on riscv32: reversed 0 2 1 3, fnv1a(openmui) 19f460cfdfe4fe76
heap ok
```

主机上算出的 `fnv1a("openmui")` 也是 `0x19f460cfdfe4fe76`。

## 附录：`std-freestanding` 的 `verbose_abort.cpp` 补丁

```cpp
// libc++ 23 把它声明在显式 ABI 注解里（注解里的每个函数都带 abi_tag 和 visibility）：
// 定义必须放在同样的注解里，否则重声明会被拒绝（"cannot add 'abi_tag' attribute"、
// "visibility does not match"）。旧版 libc++ 没有这两个宏。
#ifndef _LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
#    define _LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
#    define _LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
#endif
#ifndef _LIBCPP_OVERRIDABLE_FUNC_VIS
#    define _LIBCPP_OVERRIDABLE_FUNC_VIS
#endif
_LIBCPP_BEGIN_NAMESPACE_STD
_LIBCPP_BEGIN_EXPLICIT_ABI_ANNOTATIONS
[[noreturn]] _LIBCPP_OVERRIDABLE_FUNC_VIS void __libcpp_verbose_abort(const char*, ...) _NOEXCEPT { for (;;) { … } }
_LIBCPP_END_EXPLICIT_ABI_ANNOTATIONS
_LIBCPP_END_NAMESPACE_STD
```
