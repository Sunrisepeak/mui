# omui：静态排版与运行时共享字体交付

2026-11-04。接续[首帧与加载](2026-11-04-omui-first-frame-loading-design.md)及[官网总方案](2026-11-03-openmui-site-redesign-implementation-design.md) W8。上一检查点总传输距 5 MB 上限仅余 34,120 字节，本轮先消除同一字体内容在同一页面的重复传输；字体完整按页交付、完整中文输入字体与重程序拆分继续保留。

## 1. 真实基线

对 `12d2e15` 的实际打包页面使用独立 Chromium 156，fresh profile、HTTP cache disabled、service worker bypass，记录 CDP 请求与解压／压缩字节。没有添加测试用 URL FontFace：

| 场景 | 同一个内容 URL 的实际请求 | 多传输的压缩正文 |
|---|---|---|
| 英文首页、中文首页、英文手机 | 初始 Inter 的 CSS Font 与运行时 Fetch 各一次 | 200,731 字节／场景 |
| 英文入门文档 | 上述重复，加 JetBrains Mono 的 CSS Font 与运行时 Fetch 各一次 | 另加 91,340 字节 |

这些是同一 URL 的两份真实传输，不把 initial/full 不同资产误算重复，也不通过 HTTP cache、修改预算或删掉静态／运行时任一消费者降低数字。原报告保留在 `/tmp/omui-font-duplicates-baseline.report.omui.json`。

## 2. 通用加载器的字节租约

```js
const lease = await window.omuiLoad.fontBytes('Inter.ttf');
try {
  const face = new FontFace('Inter', lease.buffer, { weight: '100 900' });
  await face.load();
  document.fonts.add(face);
} finally {
  lease.release();
}
```

- 返回 `{id, url, buffer: ArrayBuffer, release()}`。name 必须是打包字体 basename；未知字体、当前尚不需要的字体和不可恢复 runtime 错误分别拒绝，不接受任意文件路径。
- 显式 DOM 消费者可以在 wasm 之前取字节；没有 DOM 消费者的包保留原有 preRun 启动策略。下载与 FS 安装分开：EOF 是 downloaded，只有当前计划要求的安装完成才是 ready，不能提前释放 main 的依赖。
- 按真实内容 URL 共享在途请求、验证后的字节和进度。每个 basename 仍独立安装、独立持有 runDependency；同一 URL 的多个别名不重复计字节。资产的 `names[]` 显示这些名称。
- 每份公开 lease 拥有独立 buffer 副本，调用者修改它不能污染另一消费者或待安装数据。release 幂等，清空公开 buffer；全部当前安装完成且最后租约释放后，清除私有 JS 缓存。早期租约先释放时仍保留 FS 待装字节。
- 安装后可以从 MEMFS 取得字节，无需网络重取。完整 face 已替换 canonical 路径时，晚期调用优先返回真实 full 的 id／url／字节；不能拿完整字体冒充初始 URL。
- 切换加载中的语言保留已启动传输，只重新安排安装角色。旧计划不再需要的传输明确标为 retired，保留 transferState／错误历史，从当前计划的总量、错误、完成与重试选择中排除；仍必需的字体失败继续阻塞其依赖。
- retry(error.id) 对共享物理资产恢复所有当前可安装的目标，包含 canonical 已成功而别名失败、canonical 属于 later 而别名属于 startup 的情况。修复前两条新增探针均因启动依赖无法释放失败；不能只证明再次下载成功。

解压字节是当前计划的加载进度；整个访问的线上传输另由 CDP 测量，不能用计划退役或字节缓存把已经发生的网络传输从性能报告中抹去。

## 3. 官网静态视图接入

原生注入器从实际打包字体清单产生 inert JSON 描述：body、code、chinese 三种用途。bootstrap 仅在当前页需要时请求 DOM 字体：正文 Inter、代码页 Mono、中文页 Noto。FontFace 使用租约的 ArrayBuffer，不再另发 CSS URL 请求。

URL `@font-face` 保留在 noscript 中，禁用 JavaScript 的正文仍用自己的字体。开启 JavaScript 时不会由预加载扫描器再取同一 URL。当前只按页选择静态 FontFace 消费者；运行时的全站字体计划没有因此变成完整按页交付。

加载期跨页复用已注册的 FontFace；网络／DOM 解码错误保留可读正文与重试入口。重试重新获取租约并重建失败 face，不把 DOM 解码成功当作 native 安装成功。真正呈现及目标语义就绪后删除本 bootstrap 拥有的 DOM faces，释放租约；不删除其他调用者注册的对象，不延长交接来播放加载演出。

## 4. 验证门禁

实际字体就绪后的审图：[中文手机](../../site/media/shared-font-home-zh-phone.png)、[正文及代码字体](../../site/media/shared-font-docs-en-desktop.png)。此处只暂缓公共协议发布以保持静态层可观察，native 已真实呈现；与上一轮“挂起 wasm”的截图条件不同，单独保留。

- C++ 生成器测试生成实际 HTML，包含语言条件、全语言初始 face 与同 URL／不同 basename 三个 fixture。Node 执行这些实际脚本，覆盖 gzip／流、前后消费顺序、租约隔离／释放、语言重计划、退役、网络及 FS 重试、别名安装和晚期真实 full 字节。
- `tools/ci/site-font-sharing.mjs` 用真正 Chromium、关闭缓存，检查首页中英、英文／中文手机、入门文档、字体响应挂起、运行时先消费、失败后真实点击 Retry。每个内容 URL 每次允许的尝试只有一条 CDP 请求，同时证明 DOM face 已加载及运行时安装就绪。
- 测试仅暂缓公共协议的发布来观察交接前两消费者；真实 native presented 和加载 hook 保持原行为。释放后通过真实 fontBytes 调用唤醒交接，验证 DOM faces 清理。
- 初始与完整租约按实际文件 SHA-256、长度、id、url 比较。共享下载不改变字体覆盖；原有 59 帧浏览器／原生像素与字体切换检查仍保留。
- 交付预算用停止构建及其他测试浏览器后的独立冷访问，不沿用上一候选的总传输或时间。最终结果写入官网总方案对应检查点。

## 5. 仍未完成

全站字体按页拆分、完整中文输入字形按编辑入口请求、重实验室／影片的独立程序与初始静态批次 ≤150 KB 均需继续验证。共享下载不改变 C2／R0–R6 的完成标准，功能门禁不代替美术、密度和创意 review。


## 6. 最终计量与边界

实际 C++ 生成 HTML 的 Node **135 项通过**；真实共享专项 **146 项通过**，加加载 88、结构 18、字体 19、无障碍 15、组件 40、工作台 24、动画／创意／Agent 106，共 **456 项浏览器检查通过**。GCC dev 与 LLVM ASan／UBSan 各官网 97＋生成器 29，GCC release 官网 97；完整 workspace 与其他平台不由这些局部结果代替。

逐请求计量揭示了不能省略的对照条件：既有“冷访问”会创建新 profile，但仍允许**同一访问中的 HTTP cache**。旧版 CSS 已下载的 Inter 被后续 fetch 从 cache 读取，第二个响应 encoded bytes 为 0。因此关闭 cache 的重复请求收益不能直接推广为普通冷访问总量下降。增加 `--cache enabled|disabled` 与 `--request-log FILE`，报告缓存条件与每条响应实际计数，原预算阈值未改，CI 对 cache enabled／disabled 两种条件执行同一份七项预算，并保留逐请求报告。

| 独立冷访问条件 | 12d2e15 总传输 | 本轮总传输 | 原 5 MB 预算 |
|---|---:|---:|---|
| fresh profile，访问内 cache enabled | 4,965,880 B | 4,968,558 B | 两者通过；本轮增加 2,678 B 文档管理代码 |
| fresh profile，cache disabled | 5,166,818 B | 4,968,558 B | 旧版超标，本轮通过；减少 198,260 B 总传输 |

两个本轮样本的七项预算均通过。cache enabled 的首帧代理 1812.8ms、ready 1777.0ms；disabled 为 1772.6／1739.4ms；两者 rAF p95 16.8ms、1 个长间隔、0 runtime 异常，JS heap 10.0MB／wasm 23.6MB。源样本在停止构建及其他测试浏览器后单独测量；单样本不构成稳定速度变化结论，代理仍不是 FCP／LCP，rAF 不是实际呈现帧成本。

当前总量只余 **31,442 B**，完整按页字体／输入字体及重程序拆分仍是下一优先事项。真实 Inter 初始 face 压缩正文的 CDP 字节本身约 201KB，包含字体就绪的静态批次尚未达到 150KB 目标；不能用较早的 fallback 首次绘制或 cache 命中替代。共享下载完成不等于 W8／C2 完成。

## 7. 后续集成回归与派生字体优化

上节数字归属当时的共享下载检查点。后续真实几何／材质、输入和实例策略整合至 `77979b5` 后，重新打包的冷访问为 **5,067,540 B**，超出原 5 MB 上限；没有沿用旧通过结论或提高门槛。

本轮只对派生交付字体调用公开 `compact_font_post`，将 Inter full／initial 与 Mono 的 post 字形名称移除，保留 [OpenType post 规范](https://learn.microsoft.com/en-us/typography/opentype/spec/post) 中的其它 28 字节头字段。实际消费者用 cmap／glyph ID、name family／style 与 shaping，不依赖 glyph names。源字体和许可证未写；非 post 表的 payload 和物理顺序保留，head 的 checkSumAdjustment 与目录／整字体 checksum 重算。已有 post 3／缺 post 输入逐字节幂等；目录越界／重叠／重复、checksum／post 损坏、未知版本及改动真实签名字体均明确拒绝；空 DSIG 保留。此检查不是全部字体结构有效性的证明。

真实 packer CTS 先在旧实现失败 13 条断言（post 未移除 6、坏输入未拒绝 7），其余 metadata／shaping／像素 oracle 已通过；实现后另加签名输入，共 8 种坏输入拒绝。生成器在 dev、release、LLVM ASan／UBSan 各 **5 程序、32 组通过**，真实生成 HTML 的 Node loader **135 项通过**。实际打包的两种 Inter 和 Mono 共减少 **77,672 原始字节、35,905 gzip 字节**；不是修改字体覆盖或删除产品能力。

优化后的正式 mcpp pack 为 `3441fbd`，原始 packed HTML SHA-256 `02256a78b1eba34c31915db443df3eb43335df2225dee8a710f5ee3c3e5a860c`，wasm 与上一构件逐字节相同。已重新导出 56 张开场与 8 张整帧参考，用已验证且未变化的 native 程序 SHA-256 `6fdb78a59bf6f152606c811f886baaaa2b846156aef123ec5844a715f11dac73` prerender／生成 Agent guide，然后再启动浏览器；一次未完成 prerender 就启动的检查已中止／作废，不计入通过。最终 `target/site-font-post-verified-{fonts,font-sharing,loading}.report.omui.json` 分别 **19／146／88 项通过**，含全部 59 帧与 native 像素逐帧相同及中文语言按钮在 full 字体到达前后的相同字形。

新独立 20 Mbit/s、40 ms、新 profile 冷访问的总传输为 **5,031,634 B**，相对整合回归减少 35,906 B（含 HTML 的 1 B 差异），**仍超原预算 31,634 B**；访问内 cache enabled／disabled 实测总量相同，均仅失败总传输这一项，保留原门槛；不能拿 gzip 文件求和代替网络结果。首帧代理不是 FCP／LCP，rAF 不是实际呈现成本；本轮单样本时间不构成稳定提速结论。报告 `target/site-font-post-perf*.report.omui.json` 与逐请求日志保留。按页交付／重页面拆分、静态就绪批次和完整输入字体仍开放，W8／C2 未关闭。
