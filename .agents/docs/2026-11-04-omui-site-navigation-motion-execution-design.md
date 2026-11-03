# omui 官网第二轮实施细案：导航、默认动效与创意演示

2026-11-04。维护者最新要求的执行清单，补充 [官网重构总方案](2026-11-03-openmui-site-redesign-implementation-design.md) §0.4。**本文件列出目标、实现方式和验收条件；目录或源码已有条目不等于整站已验收。** 当前各 worktree 的局部测试与整站集成验证分开记录。

## 1. Review 应先确认的设计

### 导航与首页

```text
动态 O(mui) / omui → 首页
核心特色
  UI 组件
  基础动画
  风格化
  统一接口 × 多语言
创意 demos
Agent 原生
  使用 / 开发 / 设计（页内切换，有深链接）
文档                         开始使用
```

- 核心特色是一个可用鼠标、触摸和键盘打开的导航组，Escape 关闭并恢复焦点。桌面保留简短一排；窄屏进菜单。Agent 原生是顶级产品入口，“连接”是独立工具入口，二者职责明确。
- 首页只保留短定位、一个动态 O(mui) 主演示、进入组件和创意 demos 的两个去向。主演示用同一个对象的像素／结构／脚本 agent 操作展示统一语义；一次只看一个视角，初始不展开 JSON、参数、语言代码与诊断面板。
- 标志由代码中的共享几何绘制；导航中的短组装和悬停反馈、首页较大的展开／组装共用来源。初始短运动结束后可以空闲；系统减少动态效果、用户 off/reduced 和离屏均受尊重。
- 原来的完整发布任务工作台保留在 `scenes/release`，与时间线、标志构造一起进入独立场景。首页不再堆完整工具链和四段大屏。
- 官网讲“能做什么并让人试”；文档讲安装、概念、API 和限制。核心概念、实践指南、快速开始放 `docs/...`，不直接拼到首页或产品页面下方。

### 各特色页统一行为

1. 进入页面即有明确的主对象和直观演示，先用真实变化表达特色。
2. 下方是分类／搜索／分页的轻量目录，通常只挂载一个主样张，避免逐项往下翻完整演示。移动端目录可展开收起。
3. 指针进入样张或 Tab 焦点进入控件：自动预览让位，真实控件直接接管。已有输入和选择保留，自动任务不得改回用户刚写的值；触摸直接使用控件。
4. 暂停、离屏、切页、off/reduced 停止自动演示；仍能正常完成任务。暂停后的运行时句柄必须允许主机停止请求动画帧。
5. 明确点击“查看详情”才显示状态、参数、语言、代码或语义。复制内容必须准确标注“接口表达式”“动画配置”或“完整工程”，不把短表达式当作可独立编译的工程。
6. 详情具有深链接、返回概览、重置和键盘路径。浏览器前进／后退跟随路由，不刷新整个应用；中文、英文状态和路由对齐。

## 2. 动画基础设施与组件默认动画

**组件只画状态；动画由统一槽解析。** 不在组件里增加私有计时器、逐帧脚本或各自插值公式。

- 可组合操作包括绘制匹配、字形匹配、透明度衔接、抬升、面板抬升、缩放、部件局部缩放、拉伸、阅读顺序错开、距离排序。配置的 `compose=[...]` 表示同时组合；槽的候选列表表示按档位回退，二者不可混淆。
- 40 个基础设计是不同目标和任务的可复用设计，下面逐项列出。不能把 40 个缓动名字当作 40 项设计，也不把同一个设计的中英、主题、速度重复计数。
- 同一播放器控制端点、暂停、反向、速度、归零和进度寻址。嵌套组件原有默认动效保留；一次变化收集实际活动句柄，按共同秒数控制，逐帧只读锚点，避免逐帧创建句柄数组。
- 默认配方覆盖当前 36 类基础组件及 actor log，共 37 类。类型／形状／用途决定组合，显式节点设置、应用规则、风格包和框架默认具有明确优先级，不能因增加类型规则而屏蔽风格或应用覆盖。

| 组件类别 | 默认变化与理由 | 应验证的实际表现 |
|---|---|---|
| Button、Avatar | 操作反馈、身份内容连续变化 | 焦点／按压可感知；不无目的跳动 |
| Toggle | 轨道、滑块和文字由同一状态驱动，匹配＋轻拉伸 | 滑块连续移动，快速切回不跳变 |
| Checkbox、Radio、Chip | 标记匹配＋局部缩放／淡入 | 勾选标记绕自己中心出现，不围绕整条长标签缩放 |
| Tabs、Segmented、Select、Combobox | 选择指示匹配＋轻拉伸，弹层有独立出入 | 指示器和目标一致；焦点进入／返回正常 |
| Slider、Range slider、Progress | 数值几何变化匹配；拖动即时 | 拖拽命中跟手，视觉不能拖着真实输入走 |
| Number input、Badge、Pagination、Time picker | 数值与字形连续，短时间收敛 | 数字与实际状态一致，中途再次修改可接续 |
| Text box、Code editor | 内容更新即时；焦点环和出入仍有默认动画 | 打字、输入法和粘贴不回放过期文本；已有焦点 morph 能被内容更新取消 |
| Accordion、Tree、Grid、Split view、Tag input、Breadcrumb、Actor log | 层级／布局／插入移除由结构配方统一处理 | 对象 ID 保持，展开让位，增删顺序清楚 |
| Card、Skeleton、Color picker | 表面、颜色、内容过渡；浮层另走面板槽 | 不用阴影／颜色掩盖错误布局或输入状态 |
| Tooltip、Toast、Alert | 短通知的匹配＋抬升＋淡入 | 入场与离开清晰，关闭后可回到原对象 |
| Dialog、Drawer、Popover、Date picker | 面板匹配＋轻缩放／抬升 | 正确摆放、裁切、焦点、Escape；组件样张浮层局限于舞台 |
| Stepper | 步骤状态、连线和文字统一接续 | 进度和当前步骤一致，不用泛化淡入替代变化 |

默认动画测试必须覆盖 A／中间帧／B、打断、off/reduced、风格覆盖、作用域隔离、文字即时性和真实输入。不能只断言“motion 字段存在”。组件预览局部浮层能力放运行时公共边界机制，不在官网复制另一套弹层实现。

## 3. 40 个基础设计清单

每组五项；每项交付实际 A/B、至少一个有效中间状态、真实操作、可复制配置及默认／减少动态效果表现。图形或文字类没有对应控件时，端点触发器仍控制真实状态，而不是播放预录截图。

| 分组 | ID | 名称 | 目标与区别 |
|---|---|---|---|
| appearance | `surface-color` | 表面换色 | 状态表面换一种色调，形状与位置保持连续。 |
| appearance | `outline-weight` | 边框强调 | 文档被选中时，用边框粗细传达强调。 |
| appearance | `corner-shape` | 圆角变形 | 紧凑图块过渡为圆润的标记。 |
| appearance | `depth` | 层次阴影 | 浮动便笺改变阴影层次，内容位置保持不变。 |
| appearance | `emphasis` | 主次强调 | 下一项有用的操作成为主操作，另一项退为次要。 |
| transform | `slide-card` | 横移卡片 | 文档在工作区两侧之间移动，保留对象身份。 |
| transform | `lift-card` | 抬升便笺 | 便笺从堆叠位置抬升到阅读位置。 |
| transform | `scale-card` | 整体缩放 | 预览围绕中心放大，文字随同一个对象缩放。 |
| transform | `turn-tile` | 旋转图块 | 带文字的图块围绕中心改变方向。 |
| transform | `squash-chip` | 压缩标签 | 标签在变紧凑时，以高度换取宽度。 |
| layout | `list-grid` | 列表转卡片 | 三项出行事项保留 ID，从列表变成卡片。 |
| layout | `reorder-list` | 列表重排 | 选中的曲目移到同一队列的开头。 |
| layout | `split-ratio` | 分栏比例 | 阅读栏获得更多空间，相邻栏同步收窄。 |
| layout | `accordion-size` | 详情展开 | 摘要展开为详情，下一个项目跟着让出空间。 |
| layout | `toolbar-reflow` | 工具栏重排 | 具名操作从横排变为紧凑的纵向工具组。 |
| selection | `tab-indicator` | 标签指示 | 选中标签的指示与内容一起变化。 |
| selection | `radio-focus` | 单选对焦 | 同一控件组里的选择在两个候选项之间移动。 |
| selection | `menu-highlight` | 菜单高亮 | 导航选中项的位置与强调状态发生变化。 |
| selection | `toggle-thumb` | 开关滑块 | 滑块、轨道和状态文字共同呈现同一项设置。 |
| selection | `range-handles` | 范围手柄 | 两个手柄一起调整一个有意义的区间。 |
| text | `title-replace` | 标题替换 | 相关标题切换时，保留可以匹配的字形。 |
| text | `count-change` | 数字变化 | 已完成文件数在原来的位置变化。 |
| text | `locale-label` | 中英标签 | 同一组导航标签切换语言，保留对象 ID。 |
| text | `text-align` | 文本对齐 | 标题在编辑式左对齐与居中对齐之间移动。 |
| text | `type-scale` | 字号层级 | 概览中的标签变为当前主题的标题。 |
| data | `progress-fill` | 进度填充 | 确定进度条跟随当前选定的进度数值。 |
| data | `bars-rebalance` | 柱图重分配 | 同样的三个分类重新分配数值，保留标签位置关系。 |
| data | `score-meter` | 评分刻度 | 评分标记、数字与填充轨道表达同一个值。 |
| data | `badge-count` | 徽标计数 | 收件箱的数量变化，周围的行保持原样。 |
| data | `step-advance` | 步骤推进 | 设置流程从第一步推进到复核步骤。 |
| presence | `toast-arrive` | 消息进入 | 一条新通知进入原本安静的收件箱。 |
| presence | `detail-reveal` | 详情揭示 | 便笺在自己的边界内揭示补充段落。 |
| presence | `item-insert` | 插入一项 | 新任务插入已有任务之间，原有对象为它让位。 |
| presence | `item-remove` | 移除一项 | 已完成事项离开，相邻对象合上空隙。 |
| presence | `placeholder-content` | 占位到内容 | 预留的位置变为具名人物与可阅读的消息。 |
| rhythm | `stagger-list` | 列表错开 | 按阅读顺序错开，让短行程逐项出现。 |
| rhythm | `stagger-grid` | 网格错开 | 带标签的小图块依次汇入最终网格。 |
| rhythm | `distance-stack` | 距离排序 | 相关便笺展开，按距变化中心的距离排序。 |
| rhythm | `paired-swap` | 成对交换 | 两个具名对象在同一次过渡中交换位置。 |
| rhythm | `cascade-lines` | 逐行展开 | 一段短提纲按统一节奏展开为三个可读步骤。 |

## 4. 20 个组件 × 动画 × 风格化组合

每项是有用途的小工作区。材质必须命中真实玻璃、纸墨、CRT、霓虹或液态绘制；只换颜色不算风格化效果。数据、撤销、选择和操作结果由真实模型提供。天气、价格等示例数据标为本地示例，不声称联网服务、支付或真实天气预报。

| ID / 场景 | 真实组件 | 动画与编排 | 风格 |
|---|---|---|---|
| `packing-board` / 整理一只出行包 | checkbox + card + button | spring / document | glass |
| `inbox-triage` / 给消息一个归处 | avatar + badge + card + button | ink / document | ink |
| `focus-session` / 留一段专注时间 | slider + progress + toggle + button | spring / distance | neon |
| `release-steps` / 从准备到发布 | stepper + progress + badge + button | morph / document | retro |
| `price-comparison` / 选一种付费节奏 | segmented + card + badge + button | spring / together | glass |
| `tag-sorting` / 把关键词归好类 | text_box + chip + button | ink / document | ink |
| `playback-queue` / 让下一首自然到来 | button + progress + slider + badge | spring / document | neon |
| `palette-studio` / 让颜色长成色板 | color_picker + slider + button | liquid / distance | liquid |
| `appointment-card` / 让约定成形 | date_picker + time_picker + badge + button | spring / document | glass |
| `reading-mode` / 给文字留出空间 | toggle + slider + card + button | ink / together | ink |
| `transfer-center` / 让文件各就其位 | progress + badge + card + button | morph / document | retro |
| `split-notes` / 两份笔记，一个想法 | split_view + text_box + button | spring / together | ink |
| `team-presence` / 围绕一个想法聚合 | avatar + chip + toggle + button | spring / distance | glass |
| `volume-mixer` / 把声音混成一体 | slider + progress + number_input + button | morph / document | neon |
| `consent-receipt` / 让选择清楚留痕 | checkbox + alert + card + button | ink / document | ink |
| `search-command` / 从搜索走向结果 | combobox + badge + card + button | morph / document | retro |
| `weather-plan` / 把天气变成小计划 | segmented + slider + toggle + card + button | liquid / distance | liquid |
| `budget-allocation` / 让预算有个形状 | range_slider + number_input + progress + button | spring / together | neon |
| `workspace-tabs` / 切换时，仍知身在何处 | tabs + breadcrumb + card + button | spring / document | glass |
| `issue-resolution` / 把问题变成修复记录 | alert + stepper + badge + button | ink / document | ink |

这 20 项是本轮数量下限；原方案的完整粒子归档／状态回溯／作品导出等六组旗舰需求继续单列。不能用基础组合数量达标宣布尚未实现的高级基础能力完成。

## 5. Agent 原生与多语言展示

| 页面 | 先展示的创意场景 | 查看详情之后 | 真实性边界 |
|---|---|---|---|
| Agent 使用 | 用户和脚本 agent 操作同一任务；有结果回执、操作记录、单条撤销 | 动作对象、参数、权限与失败解释 | 脚本明确标注；连接我的 agent 才进入真实配对，不能把演示说成外部模型自主完成 |
| Agent 开发 | 读取已挂载对象的真实语义，执行动作并再次读回 | 公共接口、请求／返回、复现表达式、接入方法 | 不虚构协议命令；不把模拟输出伪装运行时读回 |
| Agent 设计 | 调整实际布局、间距与局部风格；把用户值带进设计提示词 | 作用域、提示词、可复现参数和设计方法 | 用户文本 origin 标为 user_content；风格影响当前对象，官网壳稳定 |
| 统一接口 × 多语言 | 同一个真实工作台边操作边呈现共享语义 | C++／TS／Python／Lua 对应表达式与当前值 | 切语言不重建业务状态；未运行的宿主不声称已经执行；完整消费者工程验证另列 |
| 风格化 | 同一组控件默认外观与局部材料并排对照 | 圆角、参数、作用域、代码与低档替代 | 左侧默认样张与导航不受右侧风格切换影响 |

## 6. 并行实施与合入顺序

| 执行包 | 负责范围 | 独立本地检查 | 合入依赖 |
|---|---|---|---|
| A 动画 worktree | 40 基础场景、统一播放器与公共配置 | 目录唯一性、真实端点／中间帧、交互／寻址、媒体 | 公共 motion 配方和 site 接口 |
| B 组合 worktree | 20 组合、风格／统一接口／Agent 页面 | 真实效果命中、输入模型、语义读回、撤销、中英窄屏 | 共享样张模型、局部作用域、统一播放器 |
| C 组件 worktree | 组件馆重排、首页与共享动态标志 | 默认控件、搜索／选择／详情、焦点／鼠标切换、暂停／空闲、关键画面 | 运行时默认配方与局部浮层 |
| 根集成 | 默认动效、运行时修复、导航／路由、静态首帧、文档、整站真实验证 | dev／release、浏览器点击链路、尺寸／中英、性能和内存 | A/B/C 可独立继续，无需等其他平台 CI |

每个 worktree 只提交自己负责的文件，禁止共享目录中互相覆盖。合回 `openmui-design` 后做一次完整本地检查，签署 commit，推送到现有 PR #1；不另开 PR，不合并主分支。构建限制并发与磁盘占用，复用合法缓存；修改构建工具时附重复构建证据，避免“无变化也全量重编”拖慢推进。

推进顺序：默认配方与播放器合同 → 三路独立内容／布局 → 导航和静态／交互路由整合 → 本地 release 浏览器与视觉收口 → 维护者 review。任一步的跨平台 CI 排队不阻塞其他独立步骤。

## 7. 本地验收与完成记录

- **数量和复用**：40 基础设计、20 创意组合逐项盘点；不按主题／语言重复计数；可复制配置能施加到其他真实目标。
- **功能**：鼠标真实点击、Tab／Escape、触摸等价路径；预览转交互；详情、深链接、前进／后退、复制、重置、撤销。不能只用直接设 signal 代替全部交互验证。
- **动态**：看实际连续帧／动态 WebP／浏览器回放；端点正确不等于中间帧正确。暂停后帧请求空闲，离屏／切页清理，减少动态效果和用户设置均检查。
- **视觉**：英文／中文，360、390、768、1280、1440；标题与标志比例、手机 CTA、溢出、裁切、浮层和长文字逐项查看真实图片，自动布局检查辅助而不替代审图。
- **发布构建**：本地真实 release 构建和浏览器交互为检查点核心。需要的 sanitizer 与回归只在相关改动后运行；明确记录设备／编译器／版本和失败边界。
- **加载**：重新测压缩传输、首帧、字体就绪与帧时；新内容不能挤爆既有 5 MB 预算。首帧构建图、creative loading、per-page assets 是原 W8 的剩余项，继续跟踪。
- **异步 CI**：如实记 Linux／macOS／Windows 的状态；CI 发现实际跨平台问题后修，但不把等待排队或尚未跑的平台当作本地开发的前置阻塞。

| 项目 | 本轮开始时的事实 | 关闭条件 |
|---|---|---|
| 40 基础场景 | 独立 worktree 已有实现和部分真实布局／中间帧证据 | 根集成播放器、复用配置与整站浏览器验证 |
| 20 创意组合 | 已有实现，尚不能仅凭清单认定通过 | 编译、20 项效果命中、真实动作、两端点及关键连续帧 |
| 组件馆 | 新布局有独立本地测试 | 根导航、默认配方、局部浮层一起验证 |
| 首页／标志 | 本轮正在重做 | 中英实图、移动端、真实结构与交互、减少动态效果、空闲 |
| Agent／风格／多语言页 | 本轮新增实现与测试中 | 真实读回、作用域、脚本标识、输入与回执、窄屏 |
| 原高级能力与 W8 | 仍有未完成项 | 按总方案逐项交证据，不用本轮演示数量代替 |

最终进度以总方案 §15 的提交及实测记录为准。美术、创意与简洁优雅没有经过维护者 review 前，不因功能测试全绿自动标为达标。
