# 官网 wasm 体积分析（W8，C1）

2026-11-01。[总体方案](2026-11-01-openmui-master-plan-design.md) C1 的 W8 项：官网的 WebAssembly 字节花在哪里，作为 Q-38（重型页面怎么拆）和 C2 首屏预算（5 MB 以内）的依据。

## 1. 方法

- **分析构建**：工作区配置档 `wasm-size` 和发布版一样是 `-O2`，链接时加 `--profiling-funcs`，保留函数名（name 段），代码不变。
- **工具**：`omui-gen wasm-size FILE.wasm [--json] [--markdown] [--top N]`（`openmui.gen.wasm_size`）。它统计各段大小，再把每个函数体归到一个所有者：
  - 函数所属的 C++ 模块（name 段里的 `@openmui.view`）；
  - 否则归到命名空间（`omui::site`、`std`）；
  - 再否则按前缀和命名空间归到第三方库（`hb_` / `OT::` 归 HarfBuzz，`FT_` 归 FreeType，`toml::`、`nlohmann::`）。
- **数据**：`tools/ci/wasm-size.sh` 在 Pages 工作流里每次运行，报告存为制品 `wasm-size`。
- **数据段**：没有符号，单独按内容分类（TOML、WGSL、CLDR JSON、其他）。

## 2. 结果（发布版，未压缩 11.6 MB，gzip 3.5 MB）

| 段 | 大小 |
|---|---:|
| 代码 | 9.0 MiB（gzip 2.5 MiB） |
| 数据 | 2.5 MiB（gzip 0.95 MiB） |
| 其他 | < 50 KiB |

代码按所有者（前 15）：

| 所有者 | 代码 | 占比 |
|---|---:|---:|
| openmui.view | 757 KiB | 8.2% |
| HarfBuzz | 690 KiB | 7.5% |
| std（模板实例） | 629 KiB | 6.8% |
| gongfu.present2d | 470 KiB | 5.1% |
| openmui.site（含匿名命名空间 269 KiB） | 664 KiB | 7.2% |
| openmui.ui.base | 335 KiB | 3.6% |
| openmui.ui.canvas（含 kit 168 KiB） | 404 KiB | 4.4% |
| FreeType | 233 KiB | 2.5% |
| openmui.showcase.fighting | 227 KiB | 2.5% |
| openmui.render.vector | 224 KiB | 2.4% |
| libwebp + libpng | 281 KiB | 3.1% |
| openmui.core | 158 KiB | 1.7% |
| openmui.theme | 135 KiB | 1.5% |
| openmui.ui.markdown | 126 KiB | 1.4% |
| openmui.i18n | 126 KiB | 1.4% |

数据段按内容：

| 内容 | 原始 | gzip |
|---|---:|---:|
| 格斗游戏的 TOML（角色外观、受击框、招式、声音） | 629 KiB | 87 KiB |
| WGSL 着色器 | 276 KiB | 37 KiB |
| CLDR 区域数据 | 41 KiB | 4 KiB |
| 其他文本（文档、主题） | 163 KiB | 44 KiB |
| 二进制和短字符串 | 1487 KiB | — |

优化级别（同一份代码，只换级别，去掉 name 段后的发布大小）：

| 级别 | 发布大小 | gzip |
|---|---:|---:|
| `-O2`（现在） | 11.64 MB | 3.54 MB |
| `-Os` | 10.29 MB | 3.25 MB（−8%） |
| `-Oz` | 9.85 MB | 3.11 MB（−12%） |

## 3. 结论

1. **格斗游戏是最大的一块可拆部分**。`gongfu`、`gongfu.present2d`、`gongfu.data` 和 `openmui.showcase.fighting` 的代码约 0.9 MiB，加上数据段里 0.63 MiB 的 TOML，合计约 1.5 MiB（gzip 约 0.35 MiB）。它只在“作品”页里用到，却在首帧前全部下载。
2. **文本栈（HarfBuzz + FreeType，0.9 MiB）和核心（view、render、core、ui.base）是首屏必需的**，不能拆。能做的是减少模板实例（std 0.6 MiB）和换优化级别。
3. **`-Os` 换来 8% 的 gzip 体积**；`-Oz` 换来 12%，但会多出一倍的函数（内联更少），在 CPU 热路径（布局、动画求值、CPU 光栅）上会更慢。Web 的绘制在 GPU 上，CPU 热路径在 Web 上主要是布局和动画求值。
4. **图像解码（libwebp + libpng，0.28 MiB）** 只有带图片的页面需要。字体和图片资源已经按页提供，解码器还在主程序里。

## 4. 建议（Q-38 和 C2）

- **Q-38：作品和重型页面做成单独的程序，页面跳转时按需加载，不用 Emscripten 动态链接。** 格斗游戏已经是独立程序（`omui-fight`）；官网的作品页显示海报和关键帧，打开时加载它自己的 wasm。理由：
  - 主模块和侧模块的动态链接会给每个函数加重定位和导入，C++ 模块跨边界的内联也会丢失，体积和性能都更差；
  - 单独的程序复用 dist-toy 打包，一个作品一个 Pages 子路径；
  - 实验室的三个脚本宿主同理，做成实验室自己的程序。
- **C2 先做的两件事**：
  - 官网主程序去掉格斗游戏（作品页链接到它自己的页面），首帧前约 −0.35 MB；
  - 官网的 Web 配置档换成 `-Os`，约 −0.29 MB。
  前提是 `site-perf` 的帧尾（p95 16.8 ms）不变差。
- **之后**：
  - 图像解码器按需加载（作品和文档里的图片）；
  - WGSL 着色器在构建时压缩空白；
  - 观察 std 的模板实例（`omui-gen wasm-size` 的最大函数列表），把重复实例化的工具函数移出模板。
- **目标**：首帧前预算 7.5 MB → C2 的 5 MB。
  - 上面几项合计约 −0.7 MB gzip。
  - 中文字体按需加载（[性能分档](2026-11-01-openmui-performance-tiers-design.md) §7）约 −1.4 MB。
  - 首帧图先画（同上）让感知时间先到。
