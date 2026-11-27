# 文档节点的有类型属性与行内标记区间（FE1，一页设计）

状态：草案，待 review（2026-11-26）。决策：D-280、D-281（提议）。上级：[0.1.0 顶层方案](2026-11-26-release-0.1.0-top-level-design.md) 的 D-258；下游：UI1（HTML 投影）、UI2（Markdown 投影与往返 CTS）。

## 1. 事实（2026-11-26，`docs/en/guide/ui.md` 的快照与 `omui.components.markdown` 的源码）

- 语义节点的字段是 `id`、`role`、`name`、`description`、`value`、`origin`、`state`、`actions`、`bounds`、`children`（规范 §3.1）。文档的事实现在借用通用字段：

| 事实 | 现在放在哪里 | 后果 |
|---|---|---|
| 标题的级别 | `value`（规范写明） | 可用 |
| 标题的锚点 | `description` | 读屏会把锚点当描述念出来 |
| 链接的目标 | `value`，并且拼进 `description`（“标题 — 目标”） | 没有类型；HTML 投影没有写出 `href` |
| 代码的语言 | `description` | 同上 |
| 有序列表的起始号 | `value` | 没有类型 |
| 单元格的对齐、图片的来源与标题 | 没有，或在 `description` | 投影拿不到 |
| 行内格式（强调、加粗、行内代码、删除线） | 没有；`name` 是压平的文字 | 四种渲染里只有像素有 |
| HTML 注释 | 成了一个 `code` 节点，文字照画 | 指南里的 `<!-- code: … -->` 指示（lint `docs-code` 用）显示成正文 |

- 行内的链接已经是子节点（`md_link`，可聚焦、可操作），但它在所属文字里的位置没有进树。
- `mlibs.markdown` 的文档模型里这些事实都在（块的 `level`、`anchor`、`language`、`start`、`align`，行内的开闭标记，链接表）；丢失发生在“文档 → 语义节点”这一步。

## 2. 三级次序（D-185）

**① 从本体系出发。** 一棵语义树、多种渲染：把文档的事实写成树上的数据，五种渲染读同一份——像素（组件照它画）、HTML 投影、Markdown 投影、无障碍的文字属性、Agent 的快照。于是“投影不丢东西”成为可以机械检查的性质：`Markdown → 树 → Markdown` 与源解析相等；`树 → HTML` 与 `mlibs.markdown` 由源直接写出的 HTML 相等。以 DOM 为文档的体系没有这条性质——那里树是 DOM 的派生物。**结论：取这一级。**

**② 前沿与组合。** 查过的做法：

| 做法 | 形状 | 取什么 |
|---|---|---|
| 无障碍的文字属性（IAccessible2、AT-SPI 的属性区间、UIA 的文字范围） | 一段文字加属性区间 | 形状：区间。读屏已经按这个形状读格式 |
| AT Protocol 的 richtext facets；Telegram 的消息实体 | 纯文字加区间（前者按 UTF-8 字节，后者按 UTF-16 单元） | 形状已在大规模使用；偏移的单位是它们各自吃过亏的地方 |
| Portable Text | 片段带标记名，另有标记定义表 | 标记的词汇与定义分开 |
| mdast、DOM、ProseMirror 的节点 | 每段行内格式一个节点 | 不取：正文的节点数成倍增加，Agent 读到的是碎片 |
| ARIA 1.2／1.3 的行内角色（`emphasis`、`strong`、`code`、`deletion`、`insertion`、`subscript`、`superscript`、`mark`） | 词汇 | 标记的名字取自这里 |

组合：**名字取 ARIA 的行内角色，形状取区间，只有可操作的（链接、图片）才是节点**——标记指向那个子节点。

**③ 业界通行的做法**（每段格式一个元素）不取，理由见上表。

## 3. 设计

### 3.1 有类型的属性（D-280）

- 节点增加可选字段 `properties`：名字到值的映射。名字、类型与适用的角色登记在规范的 `registry/properties.json`（与 roles、states 同样是规范的数据，生成文档与类型）。
- 首批登记（只登记现在有消费者的）：

| 名字 | 类型 | 适用的角色 | 含义 |
|---|---|---|---|
| `level` | 整数 1–6 | `heading` | 级别（`value` 仍是级别，规范 0.4 的写法不变） |
| `anchor` | 文字 | `heading` | 片段标识（`#anchor`） |
| `href` | 文字 | `link` | 目标，原样（不解析） |
| `title` | 文字 | `link`、`image` | 标题 |
| `source` | 文字 | `image` | 图片的来源 |
| `language` | 文字 | `code` | 代码的语言 |
| `ordered` | 布尔 | `list` | 有序 |
| `start` | 整数 | `list` | 有序列表的起始号 |
| `tight` | 布尔 | `list` | 紧凑：各项的段落之间不留空（它改变写出的 HTML 与 Markdown，所以是事实） |
| `align` | `left`／`center`／`right` | `cell`、`column_header`、`row_header` | 对齐 |
| `raw` | 文字（`html`） | `code` | 这段是嵌在文档里的原始标记 |
| `comment` | 文字 | `none` | 注释的文字：任何渲染都不显示，往返时保留 |

- 新条款 **S-16**（S-14、S-15 已被占用）：`properties` 里的名字必须是登记过的，或是带点的厂商名（`vendor.name`）；值的类型与登记一致，节点的角色在该属性适用的角色之内（没列角色的适用于任何角色，如 `anchor`）；读到不认识的名字时保留、不赋予含义。
- 借用的字段归位：锚点、语言不再放 `description`，链接的目标不再放 `value` 与 `description`（omui 的实现在 UI1 一次改完，不留两份）。

### 3.2 行内标记区间（D-281）

- 节点增加可选字段 `marks`：`{kind, start, end, node?}` 的列表，区间落在 `name` 上，左闭右开。
- `kind` 登记在 `registry/marks.json`：`emphasis`、`strong`、`code`、`deletion`、`insertion`、`underline`、`mark`、`subscript`、`superscript`、`math`、`math_display`、`raw`、`link`、`image`。标记说的是含义，不是样子：没有颜色、字号这类。
- `link` 与 `image` 的标记带 `node`：所属节点的一个子节点的 id，目标与标题在那个子节点的属性里。可操作的东西只有一个，它在文字里的位置是这条标记。
- **偏移的单位是 Unicode 码位**（标量值）。理由：与编码无关（字节随 UTF-8，UTF-16 单元随 JS）；与 Unicode 版本无关（字素的切分随版本变，区间是存下来的数据，不能随版本移动）。文字选择仍按字素（规范已有）：光标不能落在字素中间，那是交互；这里是数据。
- 硬换行不是标记：它就是 `name` 里的换行符；软换行是空格（`mlibs.markdown` 已是这样）。
- 新条款 **S-17**：`0 ≤ start ≤ end ≤ name 的码位数`；按 `start` 升序，`start` 相同的长的在前；区间要么嵌套要么不相交（交叉的格式由写的一方拆成两段）；登记为“子节点的位置”的标记必须在 `node` 里指向一个子节点，别的标记不带 `node`；读到不认识的种类时略过。

### 3.3 注释与原始 HTML

- Markdown 里整块只是一个 HTML 注释的：角色 `none`、状态 `hidden`、属性 `comment`。像素不画；HTML 与 Markdown 投影各自写回注释；紧凑的树照旧略过 `none`，完整的树里有它。
- 其余的原始 HTML 块：角色 `code`、属性 `raw: "html"`。HTML 投影只在节点的来源是 `app` 时原样写出，`user_content` 与 `remote` 一律转义（S-3 的边界不因投影而破）。

### 3.4 归属

| 东西 | 归属 |
|---|---|
| 字段、登记表、条款、线上的形式、`check` | 规范（`spec/`，一个增补：0.4.13“文档的节点”，两个字段一起） |
| 文档到节点的填写 | `omui.components.markdown` |
| HTML 投影 | `omui.semantic`（UI1） |
| 由文档模型写出 Markdown | `mlibs.markdown`（它是这个模型的 owner）；`omui.semantic` 把快照还原成文档模型再交给它（UI2） |
| 码位与字节、字素之间的换算 | `mlibs.unicode` |
| 无障碍的文字属性 | `omui.accessibility` 的映射（不在 FE1；有了标记之后是一张表） |

### 3.5 代价

- 没有文档事实的节点：两个空的容器，不分配。`ui.md` 的快照（100 个节点）在 UI1 里量增加的字节。
- 树的摘要（`digest`）与差异（`changes_between`）把两个字段算进去。

## 4. 验收

| 任务 | 验收 |
|---|---|
| FE1 | 规范 0.4.13：两张登记表、条款 S-16／S-17、线上的形式往返、`check` 的用例（已写，本机 6 个程序通过）；规范仓库的 CI 通过并打 tag；本仓库钉住。规范的一致性场景（一篇文档）随 UI1 的轨迹补上 |
| UI1 | `ui.md` 的 HTML 投影里表格、链接（`href`）、代码（`<pre><code class="language-…">`）、行内格式逐项与 `mlibs.markdown` 由源写出的相等；注释不成正文 |
| UI2 | 最小核心文档集 `Markdown → 树 → Markdown` 往返后解析相等（CTS） |

## 4a. 实施中定下的（2026-11-26，UI1、UI2）

- **软换行是空格**：树的文字在源的行末处是一个空格（硬换行才是换行符）。“往返相等”因此按“源里的软换行读作空格”来比；HTML 上两者只差一个空白字符。
- **注释照原样**：`comment` 是定界符之间的原文（含两端的空格），写回时逐字节相同。紧凑的树保留它。
- **元素可以自己声明属性**：视图的任何元素带 `properties` 这个 prop 时并入节点的属性（单独的图片用它给出“写在文档里的来源”，而不是解析后的路径）。写进规范的视图 schema 随 ST3。
- **HTML 读回**：`mlibs.markdown::from_html` 把块之间的注释读成原始 HTML 块；代码块的子节点（复制按钮）写在 `</pre>` 之后。
- **嵌入的标记**：来源不是 `app` 的原始 HTML 在投影里是转义的文字（S-3），这样的页面读回后与源不同——这是有意的；`docs/` 里有 4 页如此。
- **Markdown 投影的位置**：`to_markdown(文档)` 在 `mlibs.markdown`；“树 → 文档”在 `omui.ui.markdown`（它本来就依赖 `mlibs.markdown`）。不带 Markdown 组件的应用若也要“读成 Markdown”，需要把 `mlibs.markdown` 的文档模型与解析器分开（否则 `omui.semantic` 会把 md4c 带进每个应用），留给 AG5 定。

## 5. 不做的

- 不为行内格式建节点；不登记样式类的标记；不允许交叉的区间。
- 不改 `value`、`description` 的含义：只是不再借用它们。
- 表格的合并单元格、脚注、定义列表：Markdown 的这个方言没有，登记表以后加一行即可。
