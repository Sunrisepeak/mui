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

