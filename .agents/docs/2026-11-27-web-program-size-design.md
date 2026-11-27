# omui 的 Web 程序的字节（红 166、红 148，一页设计）

状态：部分实施（2026-11-27，分支 `p0-size`），待 review。决策：无新的，沿用 D-185（三级次序）、D-247（Web 下载的低线约 1.1 MB）、D-284（页面自己的 HTML 是它的数据）、D-99（按库与产品建包）。上级：[0.1.0 顶层方案](2026-11-26-release-0.1.0-top-level-design.md) 的 UI7、ST2、ST6、ST7；旁及：[官网方案](2026-11-19-site-live-design-design.md) §6.1、[oweb 方案](2026-11-26-oweb-site-framework-design.md) §3.3、[体积与内存方案](2026-11-18-size-memory-performance-install-lead-design.md) §2a。

## 1. 事实（2026-11-27，`8417bfdb`，本机 Linux x64，emsdk 6.0.9，Chromium 159 无头；之前与之后都在同一份 spec 子模块上量）

“发出的字节”按 `mlibs.delivery` 的口径：静态托管压缩发出的文件取 gzip -9，其余取原样。

**红 166：文档站加载的程序（`oweb.reader`，打包成世界）。** 包里的程序是 launcher（Emscripten 的 JS）加 wasm：

| 构建 | wasm 原样 | wasm gzip | launcher gzip | 发出 |
|---|---:|---:|---:|---:|
| `--profile release`（-O2，Pages 现在的打包命令） | 8,004,455 | 2,624,460 | 54,402 | 2,678,862 |
| `--profile web`（-Oz） | 5,886,745 | 2,086,161 | 54,163 | 2,140,324 |

按归属（`omui-devtools check size -p oweb.reader`，-O2，名字保留）：代码 6.9 MiB、数据 0.99 MiB。最大的几块：omui.view 1.46 MiB（其中 `component_type<…>` 的实例 0.74 MiB，是 80 个基础组件的函数表）、HarfBuzz 0.6 MiB、omui.ui.base 0.48 MiB、libc++ 0.43 MiB、omui.semantic 0.29 MiB、Agent 面（omui.agent、mlibs.agent、协议与目录）约 0.45 MiB、viewdoc 与 expr 约 0.34 MiB、i18n 与 nlohmann 约 0.31 MiB、libpng 0.11 MiB。数据 1 MB 里 WGSL 与 ESSL 两套着色器各 0.19 MB（gzip 各约 57 KB），运行时只用一套。

**红 148：官网一页（`site/`，`omui build --web`，-Oz，D5 按首屏切两半）。** 首帧之前 1,462,941 B、合计 2,948,280 B，首帧 1028 ms（`check site delivery`，20 Mbit/s、40 ms）。整个程序 6.87 MiB（gzip 2.37）；前一半 3.24 MiB（gzip 1.11）：代码 1.73 MiB、数据 1.15 MiB——wasm-split 不切数据段，数据全部在前一半，gzip 约 416 KB。数据里：WGSL 190 KB（gzip 57 KB）、给 tsc 的 TS 声明 91 KB（25 KB）、Agent 目录的说明文字约 96 KB（35 KB）、RTTI 的类型名 112 KB（12 KB）、CLDR 三个语言包的 JSON 42 KB。

**为什么阅读器带了 80 个组件。** `AppSpec::components` 的缺省是 `&register_base_components`：只要一个程序写 `AppSpec app;`，缺省成员初始化就引用了它，链接器保留全部基础组件——即使阅读器自己只用 link、text_box 与 markdown 的部件。外壳为页面里的现场示例（```view）再登记一次。两处都去掉，-Oz 的 wasm 从 5,886,745 降到 4,778,544（gzip 2,086,161 → 1,725,394）：示例这一块占阅读器的 0.36 MB（gzip）。

## 2. 三级次序（D-185）

**① 从本体系出发。**

- *页面的 HTML 是它的数据（D-284）。* 阅读器不带文档的源与解析器的需要早已没有——页面读回自己的 HTML。由此推出：**一个文档站的页面，绝大多数没有现场示例；示例要的东西（按名字找到的全部组件、视图文档、表达式）只有带示例的页面需要。** 页面自己知道它带不带：示例的源在 HTML 里是 `<code class="language-view">`。
- *一个能力的部分，就是程序没有它时少掉的那些函数。* 同一个包里两个程序由同一批单元链接：完整的世界（`oweb-reader`），和不运行示例的同一个程序（`oweb-reader-reading`，`Layout{.examples = nullptr}`，缺省的初始化不被提到，所以不被链接）。后者的函数就是“每一页都要的”——链接器替我们算出了闭包，不靠在浏览器里跑一遍去猜。完整世界按它切成两部分：第一部分是后者有的——按符号表的名字，或按函数体（优化器把同一份代码以先遇到的那个名字合并，两次链接遇到的不一定是同一个）——再加它们在完整程序里直接调用的（两次链接在内联上也可能不同），第二部分只剩示例的。页面的 HTML 里有示例才在 main 之前取第二部分，别的页面从不取；万一第一部分调用了第二部分的函数，占位函数当场把它取来（正确性不依赖切分的精度）。
- *页面是自己的宿主，也是自己的窗口。* 无头运行（`--headless`、`--serve`、`--static-html`、`--agent-guide`、`--png`、`--replay`、`--present-to`）写文件、开端点，是程序独立运行时的事；框架画的窗口边框（D-75）在页面里没有意义。Web 上的 `omui::run` 两样都不带；播放器的 `--types` 与 `--design-film` 同样。
- *结论：取这一级。* 实施的三项见 §3。

**② 前沿与组合。**

| 做法 | 来源 | 取什么 |
|---|---|---|
| 按调用剖面切分（wasm-split 的 profile） | Binaryen；我们的 D5 已用于官网 | 官网继续用；阅读器不用——只剖一页，没跑到的阅读路径（搜索、换主题）会落进第二部分，每次用到都要同步取 |
| 按函数清单切分（`--keep-funcs`） | Binaryen wasm-split | 取：清单由兄弟程序的链接给出（①），加上 Binaryen 的直接调用图补齐 |
| Wasm 原生异常（`-fwasm-exceptions`，旧提案，Chrome 95、Firefox 100、Safari 15.2 起） | Emscripten | 量过：阅读器 -Oz 的 wasm 5,886,745 → 5,079,279（gzip 2,086,161 → 1,995,519），launcher gzip 54,163 → 51,793，合计少 93 KB（4.3%），运行时也少了 108 种 `invoke_` 的 JS 跳板。没有落地：mcpp 2026.11.26.1 的 `[abi] exceptions = "wasm"` 不给 C 单元加 `-sSUPPORT_LONGJMP=wasm`，libpng 的 `setjmp` 链接不上（`emscripten_longjmp` 未定义）；ABI 按目标不按 profile，浏览器测试的 Asyncify 与 Wasm 异常不能同用。见 §5 |
| 链接后再跑一遍 `wasm-opt -Oz` | Binaryen | 量过：-O2 的 release 再跑只少 1.5%（gzip 2,624,460 → 2,585,955）；直接用 -Oz 编译（`web` profile）少 20.5%。不另加一步 |

**③ 业界做法。** 编译期优化（-Oz）、按需加载、裁掉没用到的库，是各家共同的低线：Flutter Web 的 skwasm 渲染器约 1.1 MB、CanvasKit 约 1.5 MB（下载，不含应用自己的代码；Flutter 公布过的数，经 [LeanCode](https://leancode.co/blog/flutter-pros-and-cons-summary) 转述，[renderers 页面](https://docs.flutter.dev/platform-integration/web/renderers)现在不再写字节数）；一个小的真实 Flutter 应用用 skwasm 首次加载传 4.0 MB（[Zenn 的实测](https://zenn.dev/amam96/articles/flutter-web-canvaskit-vs-skwasm?locale=en)）；Qt for WebAssembly 的 QtCore + QtGui 最小程序 gzip 2.8 MB（[Qt 文档](https://doc.qt.io/qt-6.5/wasm.html)）；egui/eframe 的最小程序 0.33 版 gzip 277 KB、0.35 版 846 KB（[egui#8328](https://github.com/emilk/egui/issues/8328)）。我们的阅读器自己排版（HarfBuzz、双向、断行）、带无障碍与 Agent 面、CPU 与 GPU 两条路径，与 Flutter 同类；与 egui 的差距主要在 HarfBuzz、组件与 Agent 面。

## 3. 做了的（每项一个提交，字节为本机实测）

| 项 | 位置 | 对谁 | 之前 → 之后 |
|---|---|---|---|
| Web 上的 `run` 不带无头运行与窗口边框 | `omui.sdk`（各平台的 `omui:host` 说它带什么） | 每个 Web 程序 | 阅读器 -Oz：wasm 5,886,745 → 5,613,967，发出 2,140,324 → 2,051,553（−88,771） |
| 阅读器的世界分两部分：示例只给带示例的页面 | `oweb.shell`（`Layout::examples`、`holds_examples`）、`oweb.reader`（第二个程序、`program()`）、`omui.build.toy`（`part`）、`omui-gen`（`omui.gen.part`、加载器的 `needs`） | 文档站（任何“能力的部分”） | 见 §4 |
| 播放器在 Web 上不带 `--types` 与 `--design-film` | `omui.entry.player` | 每个 TS 应用的页面 | 见 §4（与第一项一起量） |

## 4. 结果

**红 166，阅读器**（`web` profile 打包；文档模板用这个世界构建，`site.delivery.xdef.json` 的行，mlibs.delivery 的 gzip）：

| | 之前（release，整体） | 之后（web，两部分） |
|---|---:|---:|
| launcher | 54,434 | 53,999 |
| 第一部分 wasm | 2,625,403（整个程序） | 1,682,063（4,648,449 原样） |
| 示例的部分 | — | 345,353（1,058,195 原样） |
| `the program`（`engine/*` 之和） | 2,679,837 | 2,081,415（−22%） |
| 一个没有示例的页面实际取的 | 2,679,837 | 1,736,062（−35%） |

第一部分里 10,522 个函数按名字或按直接调用、另 415 个按函数体（被优化器以示例的函数名合并的同一份代码）留下；只按名字切时，`/start/` 的搜索框一打字就同步取了第二部分（`omuiDeferredSync`），按函数体补齐后不再取。验证：`/start/`、`/agent/`、`/ui/`、`/live/` 四条接管行 29 项全过（三个宽度、顶部与滚动后，词最多移 0.67 px；`/live/` 的示例按钮在画布上按下有回应）；`site drive`：`/start/` 打开、在搜索框里输入两次之后 `omuiDeferredState` 仍是 `waiting`、没有同步取，搜索有 8 条结果；`/live/` 是 `instantiated`（异步，不是同步）、按 “Add one” 后读到 “Presses: 1”。每页的 `index.html` 因加载器多了 `needs` 的判断，gzip 多 222 B（`a route readable` 最大一页 14,034 → 14,256，上限 100,000）。

离 D-247 的约 1.1 MB：没有示例的页面还多约 0.64 MB；§5 的几项是下一步。

**红 148，官网**（`omui build --web`，-Oz，D5 切分，`check site delivery`，Chromium 159 无头，各跑一次；之前是 `8417bfdb`，之后含 §3 的第一、三项）：

| | 之前 | 之后 |
|---|---:|---:|
| 整个程序 | 7,205,793（gzip 2.37 MiB） | 6,960,811（gzip 2.29 MiB） |
| 前一半 | 3,396,919，gzip 1,165,535 | 3,335,960，gzip 1,146,775 |
| 前一半的数据段 | 1,174,415（gzip 416,062） | 1,156,635（gzip 409,890） |
| 首帧之前 | 1,462,941 | 1,446,961 |
| 合计 | 2,948,280 | 2,866,601 |
| 首帧 | 1028 ms | 957 ms |

`check site path` 27/27。少掉的多在后一半：无头运行与 `--types` 本来就不在首屏上跑；首帧之前只少了数据段里随它们而去的字符串（17.8 KB 原样）。给 tsc 的声明（91 KB 原样）还在数据段里——Agent 的 API 自述与运行时生成也引用它们。官网离 1.2 MB 还差 246,961 B：首帧之前的字节，九成以上是前一半的 wasm，其中数据段整段在前一半（gzip 约 410 KB），代码按首屏跑到的函数（1.70 MiB，gzip 约 0.74 MB）。

## 5. 没做的与提议

**阅读器（红 166）。**

- **Pages 的打包命令用 `web` profile。** `.github/workflows/pages.yml` 打包阅读器用 `--profile release`；`omui build --web` 在有 `web` profile 时用它（官网已是）。改成 `--profile web`：整个程序 2,678,862 → 2,140,324（−538,538，−20%，§1 的表），一行。§4 的数字都是 `web` 的；属主会话的文件，提议。
- **Wasm 原生异常**（§2 ②，−93 KB，4.3%）：先在 mcpp 上游修 `[abi] longjmp`/`exceptions = "wasm"` 不到 C 单元的问题（`-sSUPPORT_LONGJMP=wasm` 不进 `cflags`，profile 与 target 的 `cflags` 也到不了索引包的 C 单元）；再决定 Web 的 ABI 是否整体换成 Wasm 异常（浏览器测试的 Asyncify 要换成 JSPI，或单留一条 JS 异常的车道）。
- **只带一套着色器**（mrender，属另一会话）：WGSL 与 ESSL 各约 190 KB 原样、57 KB gzip，都在数据段。按选中的后端取，阅读器的第一部分少约 57 KB。
- **HarfBuzz 的裁剪**（mcpp-index 的 `compat.harfbuzz` 加 feature）：`HB_LEAN` 去掉了我们要的绘制、颜色、变体与 CFF，不能用；逐项的 `HB_NO_AAT`、`HB_NO_BUFFER_SERIALIZE`、`HB_NO_BUFFER_VERIFY`、`HB_NO_BITMAP`、`HB_NO_MATH`、`HB_NO_META`、`HB_NO_STYLE`、`HB_NO_VERTICAL`、`HB_NO_LAYOUT_UNUSED`、`HB_NO_OT_FONT_GLYPH_NAMES` 可以；未量（阅读器里 HarfBuzz 约 0.33 MiB 代码）。
- **Markdown 的解析器**：阅读器只读页面的 HTML（`from_html`），md4c 与解析路径（-Oz 约 60 KB 代码）仍被 `markdown()` 的运行时格式分支链接进来。解析做成 omui.ui.markdown 的一个槽，只读 HTML 的程序就不带。
- **CLDR 与 i18n 的读法**：三个语言包的 JSON 42 KB 原样在数据段，启动时由 nlohmann 读（i18n 与 nlohmann -Oz 约 190 KB 代码）；阅读器不按语言格式化任何东西。i18n 改用 mlibs.json（已在程序里）读，nlohmann 就可能整个离开；语言包做成 sdk 的一个 reduction。
- **量过、不取：阅读器不带 sdk 的 `images`。** 页面的图由浏览器解码（D-284），`images` 只装 PNG 读取器的槽；去掉后第一部分只少 222 B（gzip 1,684,924 → 1,684,702）：omui.agent 的截图比较直接调用 `decode_png`，libpng 的读取仍被链接。归 Agent 面。
- **Agent 面**（约 0.3 MiB 代码，window.omui 的协议、目录与说明）：照 D-174 保留在每个程序里；可以和示例一样做成一个部分，在页面第一次收到 Agent 的请求时取——要先定“Agent 第一次请求时等多久”的契约。

**官网（红 148，首帧之前还差 246,961 B）。** 首帧之前的大头在前一半 wasm 里；按本机量到的份额（前一半，gzip）：

| 项 | 份额 | 归属 |
|---|---:|---|
| WGSL 着色器（只用一套的另一套不进首帧，或按后端取） | 约 57 KB | mrender |
| Agent 目录的说明文字（D-186 的文字作为数据在接入时取） | 约 35 KB | omui.agent、mlibs.agent |
| 给 tsc 的 TS 声明（数据，Agent 的 API 自述也引用） | 约 25 KB | omui.lang.js、omui.agent |
| Wasm 原生异常 | 约 4%，约 45 KB | mcpp、工作区 ABI |
| CLDR 的 JSON 与 i18n 的 nlohmann 读法（首屏跑 18.8 KB 的 `Reader::read`） | 约 20 KB | mlibs.i18n |
| 清单在构建时读成 JSON（toml++ 29 KB 代码在首屏） | 约 12 KB | omui.player、打包器 |
| 脚本的类型在打包时擦掉（`mlibs::ts` 32 KB 在首屏） | 约 12 KB | 打包器、omui.lang |
| RTTI 的类型名（112 KB 原样） | 约 12 KB | 全工作区 `-fno-rtti` 的可行性要先查 |
| Web 上不解析命令行（cmdline 与 `command_line` 26 KB 在首屏） | 约 10 KB | omui.sdk |

合起来约 230 KB，与差距同一量级：确定能做，分在几个属主，没有一项单独就够。

## 6. 属主与边界

- 改了的：`omui.sdk`（各平台的 `omui:host`、`app.cpp`）、`omui.entry.player`、`oweb.shell`、`oweb.reader`、`omui.build.toy`、`omui-gen`（新模块 `omui.gen.part`、`dist_toy` 的加载器）、`omui-devtools site drive`（站点里一条路由的目录连同站点一起服务）。
- 没动的：mrender（着色器的提议在 §5）、`modules/projects/oweb/src`（`the program` 行仍是 `engine/*` 之和：两部分都算；是否另记“没有示例的页面取的”是属主的事）、`.github/workflows/pages.yml`（`--profile web` 的提议在 §5）、`omui_web.js` 与镜像。
- 新的能力是通用的：任何一个包都可以用“同包里少一个能力的程序”把自己的世界切成部分（`.part`），按页面文档里的选择器取；不止阅读器的示例。


## 7. 一页只取一套着色器：程序把着色器的文字放在旁边（第二轮，2026-11-27，分支 `p0-size2`，起点 `ed1db276`）

**事实。** 阅读器第一部分的数据段 962,634 B 原样（单独 gzip 362,230 B），其中 WGSL 107,269 B（21 个着色器文件的 27 个入口，单独 gzip 31,179 B）、ESSL 82,308 B（26 个入口，27,366 B）：每个入口两套，都在数据段里，wasm-split 不切数据，两套都在每页必取的那一半。运行时一页只用一套：有 WebGPU 时 WGSL，只有 WebGL2 时 ESSL，GPU 是软件的（无头 Chromium 的 SwiftShader）或没有 GPU 时 CPU 画，两套都不用。§5 说的“各 57 KB gzip”是按 16 KB 分块各自压缩的估计，偏大；按整段压缩是上面的数。

**① 从本体系出发。**

- *程序自己从不读这些文字。* 着色器的文字只有页面一侧的设备读（`omui_webgpu.js` 与 `omui_webgl.js` 按指针与长度解码后交给浏览器编译），C++ 只传指针。所以它们可以不在程序里，而在页面旁边，由设备在需要时取回——程序的行为不变，`rhi::ShaderCode` 的契约不变（仍是格式与字节；字节的第一个字节为 0 时，页面按地址给出那段文字）。
- *谁画，在程序加载时就知道。* WebGPU 的设备本来就在加载时（一个运行依赖里）向浏览器要 adapter；宿主的 `automatic` 选法（软件的 adapter 算没有 GPU，`?renderer=` 优先）用的是同一组事实。于是设备在同一个运行依赖里顺便取它那一套：硬件 adapter、或页面要了 GPU 时取 WGSL；页面没有 WebGPU、或要了 WebGL2 时，WebGL 一侧取 ESSL。CPU 画的页面两套都不取。猜错（例如有 `navigator.gpu` 却拿不到 adapter、而 WebGL2 是硬件的）只是第一次建管线时同步取一次，画面不错。
- *打包器本来就在改写程序。* “一个能力的部分”（§2 ①）是代码按能力切；这里是数据的对应物：生成器在数据前写一个标记（`mlibs.apart`，与领域无关），打包器（`omui-gen` 的 `carry_apart`）在链接好的程序里找到标记，把那一段与标记置零（函数与下标都不变，所以“部分”的切分照旧），每种写成一个文件 `<程序>-<hash>.<种类>.json`（地址 → 文字），加载器给出 `Module.omuiApart`（`want`、`texts`）。没有打包的程序（测试、`mcpp run` 的产物）文字在原处，照原样读。
- *结论：取这一级。* 标记与找标记是一处（`mlibs.apart`），写标记的是着色器的生成器（`mlibs.shader` 的 `emit_cpp`，只给 WGSL 与 ESSL；原生格式的输出逐字节不变），取回的是两个设备，按种类各取各的。

**② 前沿与组合（试过或查过，不取）。**

| 做法 | 结论 |
|---|---|
| 页面里把一套翻译成另一套（Tint、Naga 编成 wasm） | 要把一个着色器翻译器（前端、中间表示、后端）带进每一页，省下的只是 gzip 约 27 KB 的一套；未量，不取 |
| 每个后端链接一个程序（WGSL 一个 wasm、ESSL 一个），加载器按 `navigator.gpu` 选 | 打包与 D5、示例的切分都要做两遍；CPU 画的页面仍带一套 |
| Emscripten 的 `--preload-file` 或 `.data` 包 | 全有或全无、在 main 之前取，两套照样都取；dist-toy 也拒绝 `.data`（页面的文件是带 hash 的资源） |
| wasm 的自定义段（custom section）放文字 | 仍在 wasm 文件里，照样传输；程序也不能按地址读它 |
| Binaryen 的 `--memory-packing` | 只去掉零，不改变哪些数据在程序里 |

**③ 业界做法。** 用 Emscripten 的通用机制（数据段、`--embed-file`、`--preload-file`）把着色器随程序一起发，每种后端的变体都在包里。没有取。

**实施**（提交 `a4f43f13` 为第一步，字节与验证为第二步）：`mlibs.apart`（mlibs.utils）；`mlibs.shader` 的 `emit_cpp`；`omui-gen` 的 `carry_apart`（`omui.gen.part`）与 `dist_toy`（写旁边的文件、加载器的 `Module.omuiApart`；“部分”的切分读打包后的程序）；`mrender.rhi.webgpu` 与 `mrender.rhi.webgl` 的页面一侧（`start` 里取自己那一套，`text()` 按地址读）。`omui-devtools check browser` 从生成的分区里取每个入口的文字时，取标记之后的那一段（与 `ShaderCode` 的范围一致）。

**结果**（2026-11-27，本机 Linux x64，emsdk 6.0.9；阅读器 `mcpp pack --profile web`，文档模板 `site.build --engine` 用它，`site.delivery.xdef.json` 的行，mlibs.delivery 的 gzip）：

| | 之前（`ed1db276`） | 之后 |
|---|---:|---:|
| launcher | 54,021 | 54,299 |
| 第一部分 wasm | 1,682,409 | 1,625,233 |
| 示例的部分 | 345,327 | 345,361 |
| WGSL（WebGPU 画的页面才取） | — | 30,484 |
| ESSL（WebGL2 画的页面才取） | — | 26,962 |
| `the program`（`engine/*` 之和，两套都算） | 2,081,757 | 2,082,339 |
| 页面自己的 HTML（加载器多了 `omuiApart`，最大一页） | 14,255 | 14,792 |
| 一个没有示例、CPU 画的页面取的程序（launcher 与第一部分） | 1,736,430 | 1,679,532（−56,898） |
| 同上，WebGPU 画 | 1,736,430 | 1,710,016（−26,414） |
| 同上，WebGL2 画 | 1,736,430 | 1,706,494（−29,936） |

验证：`check takeover` 的 `/start/` 7 项、`/live/` 8 项全过（含浏览器的查找）；`site drive` 在无头 Chromium 159（`--enable-unsafe-webgpu`，SwiftShader）里以 `?renderer=webgpu` 与 `?renderer=webgl` 打开 `/start/`：`omuiApartState` 分别是 `{"wgsl":"held"}` 与 `{"essl":"held"}`（加载时取到，没有同步取），没有警告；截图与之前的世界逐字节相同（WebGL2 画出整页；无头的 WebGPU 不能呈现到画布上，之前与之后都是空白）。`mcpp test` 的 `mlibs.utils`（`apart` 4 例）、`mlibs.shader`（15 例，含标记的输出）、`omui.gen`（50 例，含 `carry_apart` 的置零与只取一次）；两个设备页面一侧的 node 测试（各 12 例，含取哪一套、按地址读、读不到时失败并说明）。没有在真实 GPU 的浏览器里验证（本机的有界面浏览器上 WebGPU 是硬件的那条路径）：标“未验证”。

**红 148，官网**（`site/` 里 `omui build --web`，复制打包出的页面与符号表，`omui-devtools site split`（D5），`check site delivery` 与 `check site path`，Chromium 159 无头，各跑一次；之前是主会话在 `ed1db276` 上量的，同一台机器、同一组步骤）：首帧之前 1,446,961 → 1,386,630（−60,331），合计 2,866,601 → 2,809,521，首帧 957 → 860 ms；`check site path` 27/27。无头的 Chromium 用 SwiftShader，CPU 画，两套都不取。官网离 1.2 MB 还差 186,630 B。

**Wasm 原生异常（§2 ②、§5）。** 归约成三个文件的复现（一个调用 `setjmp` 的 C 单元、一个 `try` 的 C++ 单元、`[target.'cfg(os = "emscripten")'.abi] exceptions = "wasm"`）：C++ 单元与链接有 `-fwasm-exceptions`，C 单元两样都没有，链接报 `emscripten_longjmp` 未定义；给 C 单元加 `-sSUPPORT_LONGJMP=wasm` 就链接、在 node 里运行输出 `ok`。已报上游：[mcpp#1029](https://github.com/mcpp-community/mcpp/issues/1029)（2026-11-27）。本仓库不绕开；等它修好再决定 Web 的 ABI 是否整体换（浏览器测试的 Asyncify 与 Wasm 异常的取舍仍在 §5）。

## 8. 解析器：i18n 改用 mlibs.json（第二轮）

**事实。** 阅读器链接了 nlohmann 的两个实例：`ordered_json`（mlibs.json 的 `Document`，程序本来就用它读 JSON）与 `json`（`std::map` 的树，只有 mlibs.i18n 读 CLDR 的语言包时用；omui.core 另用 `json` 的 SAX 解析器建 `Value`）。i18n 的读法换成 mlibs.json 的 `Node`，第二棵树就不在程序里了。

**三级次序。** ① 程序里已有一个读 JSON 文档的属主（mlibs.json：带位置的问题，D-139），i18n 是第二个读法，按单一归属就该用它——问题的路径不变，解析失败时多了行与列。② 把 CLDR 在打包时生成成代码（不解析）：`LocaleData` 满是 `std::string` 与 `std::vector`，生成出的构造代码不会比 42 KB 的 JSON 小，且 CLDR 的数据仍在；未取，见下面的提议。③ 业界的 i18n 库多自带数据格式与读法（ICU 的 `.res`），即再多一个读法；不取。

**结果**（同 §7 的量法；之前是 §7 之后的世界）：第一部分 1,625,233 → 1,621,613（−3,620），launcher 54,299 → 54,268，示例的部分 345,361 → 345,466；一个没有示例、CPU 画的页面 1,679,532 → 1,675,881（−3,651）；`the program` 2,082,339 → 2,078,794。比 §5 估的少：`json` 的词法与 SAX 解析仍随 omui.core 的 `Value` 读法留在程序里（nlohmann 的代码 49.2 KiB 原样，`omui-gen size`），只少了 i18n 那一棵树与它的访问。验证：`mcpp test -p mlibs.i18n -p mlibs.i18n.cldr`（三个语言包与 likely 读出、问题仍按路径报）；`check takeover` `/start/` 7/7。

**提议（未做）。** omui.core 的 `Value` 也用 mlibs.json 读（或 mlibs.json 只留一个解析器实例），nlohmann 的 `json` 才整个离开；属主是 omui.core，改动面在它的读法与问题的位置上，单独一步。

## 9. HarfBuzz 的 `HB_NO_*`：量了，要改索引（第二轮）

**量法**（2026-11-27，本机，emsdk 6.0.9）：`target/` 下的探针，只链接 mlibs.text.harfbuzz 调用的 HarfBuzz 函数（它三个原生单元里的 57 个 `hb_*`），用构建 Web 程序时 HarfBuzz 的同一组编译参数（`-Oz -std=c++23 -fexceptions -ffunction-sections -fdata-sections -DNDEBUG`，`compile_commands.json` 里 `harfbuzz.cc` 的那一行），只换宏：

| 宏 | wasm 原样 | gzip | 对我们的行为 |
|---|---:|---:|---|
| 无（现在） | 556,891 | 218,946 | — |
| A：`HB_NO_BUFFER_SERIALIZE`、`HB_NO_BUFFER_VERIFY`、`HB_NO_BUFFER_MESSAGE`、`HB_NO_MATH`、`HB_NO_META`、`HB_NO_LAYOUT_UNUSED`、`HB_NO_OT_FONT_GLYPH_NAMES`、`HB_NO_STYLE` | 531,727 | 208,442（−10,504） | 不变：这些 API 我们一个都不调 |
| B：A 加 `HB_NO_AAT`、`HB_NO_VERTICAL` | 461,992 | 187,747（−31,199） | 横排不变（整形只设 LTR、RTL）；只有 AAT 表（`morx`、`kerx`）而没有 OpenType 表的字体整形会变：页面带的字体（Inter、Noto Sans SC、JetBrains Mono）没有，但应用可以载入别的字体，所以 B 是一个“减项”，不是默认 |
| 对照：只给 HarfBuzz `-fno-exceptions` | 534,640 | 216,067（−2,879） | 不变（HarfBuzz 不抛异常）；小，不取 |

`HB_NO_BITMAP` 不在里面：我们调 `hb_ot_color_has_png`（字体有没有 PNG 字形），去掉位图表会改变带这种字形的字体的画面。`HB_LEAN` 去掉绘制、颜色、变体与 CFF，不能用（§5）。

**三级次序。** ① 从本体系出发：我们的整形只调 57 个函数、只排横向，宏按“程序用到什么”取，开关用索引已有的 feature 机制（与 `no-freetype` 同一条路）。② 上游自己的预设 `HB_LEAN`、`HB_TINY`：去掉的包括我们要的绘制、颜色、变体，不取。③ 各家通常整套链接 HarfBuzz；不取。

**为什么停在这里。** 宏只能进 HarfBuzz 自己的单元：它是 mcpp-index 的 `compat.harfbuzz`，单元的参数由索引里的描述给出（`mcpp_generated/config.h` 由描述生成，`no-freetype` 就是这样一个开关，`MCPP_FEATURE_NO_FREETYPE` 在生成的 `config.h` 里读）；工作区的 `cflags` 到不了索引包的单元（§7 末尾，mcpp#1029 的旁注）。要做的是索引里的一个改动：`compat.harfbuzz` 加两个 feature——`no-unused-api`（A，读 `MCPP_FEATURE_NO_UNUSED_API`，在生成的 `config.h` 里定义 A 的八个宏）与 `no-aat-vertical`（B 多出的两个）——描述的 `revision` 加一，再由 `mlibs.text.harfbuzz` 选 `features = ["no-freetype", "no-unused-api"]`；B 作为 omui.sdk 的一个减项（`minimal` 那一类）给明确不载入 AAT 字体的作品。没有改共享的包缓存去量（别的会话在用），数字来自上面的探针，在程序里的实际份额以落地后重量为准。

## 10. 清单在打包时读成 JSON：Web 程序里没有 TOML 解析器（第二轮）

**事实。** 官网的页面是 TS 应用，播放器在首屏读应用的 `mcpp.toml`（`[package.metadata.openmui]`：标题、尺寸、脚本、主题），用 toml++ 读——toml++ 的代码在前一半（§5：约 29 KB 原样，gzip 约 12 KB）。

**三级次序。** ① 打包器本来就把应用的文件写进页面，数据模块（`omui.config.ts`）已经是“在打包时求值、页面只读快照”；TOML 是同一类：打包器把 TOML 文件写成它的 JSON 快照（`<路径>.json`，toml++ 自己的 `json_formatter`），Web 上的播放器读快照。声明只有一个读法（`omui.entry.player` 的 `manifest_of`，经由 mlibs.json）：原生上先把 `mcpp.toml` 用 toml++ 写成 JSON 再交给它，Web 上直接读快照，toml++ 只在原生的依赖里。② 在页面里换一个更小的 TOML 解析器：仍是一个解析器、第二个实现，不取。③ 业界的 Web 打包通常把配置在构建时编进产物；我们就是这样做，用的是已有的快照机制。

**与红 145 的关系。** 145 要的是原生的 `omui-launch` 不带 TOML 解析器，读的是安装在用户机器上的应用，快照要由 `omui`（构建或打包时）写在应用旁边——写的一方不同，是另一个改动；本步给出的“一个读法读 JSON 声明”可以在那时复用（`manifest_of` 是播放器的，launcher 读的是 `runtime` 一节）。没有做 145。

**结果**（官网，`omui build --web`、`site split`、`check site delivery`，同 §7；之前是 §7 之后、§8 之前的那次，之后含 §8 与本节——阅读器不经过播放器，§8 在官网上的份额没有单独量）：首帧之前 1,386,630 → 1,371,883（−14,747），合计 2,809,521 → 2,782,460，首帧 860 → 866 ms；页面里是 `/mcpp.toml.json`，程序的符号表里没有 `toml::`；`check site path` 27/27。`mcpp test -p omui.gen`（50 例，含 TOML 写成快照、坏的 TOML 说出位置）；播放器原生构建通过。

## 11. 量过或看过、这一轮没做的（第二轮）

- **Markdown 的解析器做成槽（红 166）。** 阅读器里 md4c 33.5 KiB 代码（`omui-gen size`，`web` profile 的整个程序），另有 `mlibs.markdown` 的流式解析一段。它在程序里，是因为 `omui.ui.markdown` 的 `format` 是运行时的属性：组件的虚函数表同时到达 HTML（`from_html`）与 Markdown 源（`Stream` → md4c）两条路。做成槽，就要每个显示 Markdown 源的程序在根上装解析器：播放器（TS 应用）、三个示例、一个作品、xtools、omui 命令行、oweb 的命令（`modules/projects/oweb/src`，不在本分支的边界内）。要先定：谁装（sdk 的 compose 行，只在程序链接了这个组件时——sdk 不依赖它），装不上时组件说什么。未做，提议。
- **脚本的类型在打包时擦掉（红 148，约 12 KB 移出前一半）。** 官网带设计器（`designer.release`），设计器编辑的是 TS 源（D-149：TS 源是唯一的事实），页面只带擦过的 JS 就不能再编辑；两样都带，每页多出脚本的字节。要先定：带设计器的发布页面是否两样都带。未做。
- **Web 上不解析命令行（约 10 KB）。** Web 上的地址 `#/…` 就是程序的开关（`omuiArguments`），应用自己的开关也声明在同一个 `App` 里；Web 另写一个解析器是第二个实现。不取。
- **Agent 目录的说明文字与给 tsc 的声明（官网前一半约 35 KB 与 25 KB）。** §7 的“放在旁边”现在只给 JS 读的数据；这些文字由 C++ 在 Agent 来问时读，要在第一次读之前放回内存（`mlibs.apart` 里一个由 Web 宿主装上的钩子，读的一方经由它），而 Web 宿主的 Agent 端点正在换成 `agent::ProcessSession`（AG5 第三步，主会话的 `web.cpp`）。等它落地、合进来后再量（主分支上阅读器的世界因 AG5 第二步第一部分多了 16.4 kB gzip）。
- **CLDR。** 每个程序启动时读三个语言包（复数规则、人的语言的格式），首帧要用；打包时生成成代码也去不掉数据。不取。
- **omui.core 的 `Value` 读法**（§8 的提议）：它用 nlohmann `json` 的 SAX 解析器，是 nlohmann 留在程序里的第二个实例的大半；改成与 mlibs.json 同一个实例（`ordered_json` 的 SAX，或经由 mlibs.json）。属主 omui.core，不在本分支的边界内。

## 12. 合进主分支之后（2026-11-27，本分支 `b1632af0` 合了 `bd9db9be`：AG5 第三步与 p0-scroll）

同 §7 的量法，本机：

| | `ed1db276`（第二轮起点） | 合并之后 |
|---|---:|---:|
| 阅读器：没有示例、CPU 画的页面取的程序（launcher 与第一部分） | 1,736,430 | 1,693,614（54,487 + 1,639,127） |
| 同上，WebGPU 画（加 WGSL 30,484） | 1,736,430 | 1,724,098 |
| 同上，WebGL2 画（加 ESSL 26,962） | 1,736,430 | 1,720,576 |
| 阅读器：`the program`（`engine/*` 之和，两套着色器都算） | 2,081,757 | 2,096,286 |
| 文档站最大一页的 HTML | 14,255 | 15,035 |
| 官网：首帧之前 | 1,446,961 | 1,372,935 |
| 官网：合计 | 2,866,601 | 2,783,729 |
| 官网：首帧 | 957 ms | 870 ms |

主分支在这期间也给阅读器加了东西（AG5：站点的主体、页面里的 Agent 门；第二步时第一部分多 16.4 kB gzip），所以“合并之后”一列是两边之和；本轮各步各自的差在 §7、§8、§10。`check takeover` `/start/` 7/7、`/live/` 8/8，`check site path` 27/27。离 D-247 的约 1.1 MB：阅读器 CPU 画的页面还多约 0.59 MB；官网离 1.2 MB 还差 172,935 B。剩下的按字节排在 §9 与 §11。

## 13. 第三轮（2026-11-27，分支 `p0-size3`，起点 `d57aa843`，内容与 §12 的树相同）

### 13.1 给 tsc 的声明：Web 程序不带

**事实。** 官网前一半的数据里有三段 TypeScript 声明：规范的绑定 API（`openmui.d.ts`，16.9 KB 原样的分区，gzip 约 4.9 KB）、`openmui/define`（约 1.9 KB）、`omui/define`（omui.lang.ts，32 KB 原样，gzip 约 10.4 KB）。它们只由 `declare_typescript` 读——写 `omui check` 的 tsc 用的声明文件；它挂在 TypeScript 语言的 `declare` 上（`std::function`），只要构造了这个语言就被链接，哪怕 Web 上从不调用（播放器的 `--types` 上一轮已只在桌面上）。

**三级次序。** ① 页面不做类型检查：`declare` 是桌面的事，Web 上的 TypeScript 语言不带它（`declaring()` 按平台给：`src/checker/` 只在桌面编译，`src/native/web/declaring.cpp` 给空的），声明随之离开 Web 程序，不是放在旁边。Agent 写脚本要读的 API（`describe`，`design.api`）不动：它用 `components_of` 与 `ts::declare_components`，留在两边共用的单元里。② 把声明放在旁边、第一次读时取回（§13.2 的机制）：Web 上没有读的一方，取回也是多余的；不取。③ 无。

**结果**（官网，同 §7 的步骤，Chromium 159 无头，本机）：首帧之前 1,372,935 → 1,357,344（−15,591），合计 2,783,729 → 2,759,557（−24,172），首帧 870 → 860 ms；`check site path` 27/27；前一半的数据段里三段声明都不在了，符号表里没有 `declare_typescript`。阅读器不带 TS，不变。`mcpp test -p omui.lang.js`（32 例，含声明的写出）。

### 13.2 程序自己读的数据：放在旁边，第一次读之前放回（`mlibs.apart` 的第二种）

**事实。** 每个操作目录（ops-gen 生成）都带一份清单（`<产品>.ops.json`，JSON 原文），Agent 的 `describe … detail: full` 原样回答它；AG5 之后，Web 宿主的端点是内核的 `agent::ProcessSession`，页面的 WebMCP 在浏览器有 `document.modelContext` 时每个主体问一次清单。阅读器里有两份：omui.agent 的 `ui`（`omui.ops.json`，23.5 KB 原样）与阅读器自己的 `site`（3.1 KB）；官网里是 `ui` 那一份（设计器与生成的清单只有命令行工具读，没有链接进页面）。一个从不问 Agent 的页面也取了它们。

**设计。** §7 的“放在旁边”只给页面一侧（JS）读的数据；这里是 C++ 读的：
- *谁先读。* 清单只在一处被读：内核回答 `describe … detail: full` 时（`mlibs.agent` 的 `subject.cpp`，经由 `Subject::listing()`）。那里改成 `mlibs::apart::present(listing)`：区域的第一个字节是它自己的（JSON 以 `{` 开头）就原样回答；是 0（打包器拿走了）就先请宿主放回再回答；放不回就当没有清单，回答主体的简述。
- *原生与无头测试照旧。* 没有谁装放回的办法（`bring_with`），也没有谁拿走过：原生构建、无头测试、没打包的页面里，区域就在原处，`present` 当场返回。
- *页面。* `omui.sdk` 的 Web 运行（`shown`）装上放回的办法：`src/js/omui_apart.js` 向加载器要这一种（`Module.omuiApart.texts('data')`，第一次要时才取那个文件），把每段按地址写回程序的内存。之后再读，第一个字节已经不是 0，不再取。
- *从不问的页面。* 什么都不付：文件不取，代码只多一个函数。
- *生成。* ops-gen 把清单写在标记之后（一个模块内的数组，导出的 `listing` 是指向它的视图，导出面只换了初始化的写法）；仓库里 27 个目录全部重新生成，它们的导出面重写（lint `api`）。
- *为什么不是“页面在加载时取”。* §7 的着色器在加载时由设备决定取哪一套；清单要等 Agent 来问，问的时机（WebMCP、连上的 Agent）在程序之外，提前取就是让不问的页面也付。第一次问时同步取一次（与 §4 的第二部分同一个后备办法）。

**三级次序。** ① 如上：清单只有一个读的地方，打包器本来就能把数据放在旁边，宿主本来就是页面与程序之间的那一层。② 让 WebMCP 在 JS 里直接读清单文件、不经 C++：清单只是 `describe` 的一种回答，绕开内核就成了第二条路（权限与主体的判定都在内核里），不取。③ 各家的做法是把工具清单编进程序，或由服务端提供；没有取。

**结果**（阅读器，同 §7 的步骤）：第一部分 1,639,127 → 1,634,870（−4,257），launcher 54,487 → 54,663（+176，放回的 JS）；一个没有示例、CPU 画、不问 Agent 的页面 1,693,614 → 1,689,533（−4,081）；清单文件 `data.json` 26,319 B 原样、5,278 B gzip，只有 Agent 来问时取。`check takeover` `/start/` 7/7、`/live/` 8/8。`site drive`（`/start/`）：问之前 `omuiApartState` 里没有 `data`（没取）；`subjects` 是 `ui` 与 `site`，两者的 `describe … detail: full` 都完整回来（`site` 4 个工具、`omui` 32 个），之后 `omuiApartState.data` 是 `synchronous`（取了一次）。

官网（同 §7 的步骤）：首帧之前 1,357,344 → 1,352,304（−5,040），合计 2,759,557 → 2,754,697，首帧 864 ms；`check site path` 27/27（其中访客的 Agent 经 `omui connect` 连上并操作页面）。剩下的 Agent 文字——目录表里的说明、参数的说明、入门的任务（`CatalogEntry`、`Guide`）——由派发与 `describe` 的简述在很多处读，要逐处经由 `present`；没做，见 §13.5。
