# Agent

每个 omui 应用天生能被 Agent 读懂和操作：它读的是语义树，做的是节点上的动作，和人用的是同一套。不需要截图识别，也不需要你另写一套接口。

## 命令行

```sh
omui agent ls                                   # 哪些应用在运行
omui agent snapshot <app> --interactive         # 可操作的节点：角色、名字、值、状态、动作
omui agent find <app> '{"role":"button","name":"Save*"}'
omui agent act <app> <node> <action> [--args JSON]
omui agent wait <app> --settled                 # 等界面静下来
omui agent capture <app> --out shot.png         # 需要看的时候截图；--format svg 取精确的矢量
omui agent undo <app>                           # 撤销
```

输出是一行一个 JSON；退出码 0 完成、1 失败、2 需要人回答、3 还没准备好。

## 接到 Claude Code 等 Agent 上

```sh
omui mcp            # 一个 MCP 服务：读树、找节点、操作、等待、截图
```

把它登记为 Agent 的 MCP 服务后，Agent 就能直接驾驶你正在运行的应用。应用自己不需要改任何代码。

## 谁能做什么

- 动作分三类：**只读**、**可撤销**、**不可撤销或对外**（删除、发送、付款）。第三类默认要人确认：命令返回 2，带上 `--answer` 再来一次才执行。
- Agent 不能冒充人，不能替自己批准。
- 权限由人定，写在 `omui.config.ts`；`omui new` 写下的就是这一份（开发时全开；发布后不可撤销的先问人）：

<!-- code: modules/projects/omui/sdk/templates/app-ts/omui.config.ts.in -->
```ts
// How far agents may go and what the recorder keeps, in development (`omui
// dev`) and in a release (`omui run`). These are the defaults; a switch given
// at launch (--agent-mode) wins over them.
//   agent:  development — every control a developer's own tools have; whether
//           to ask you first is the agent's judgement and your instructions
//           to it. dual-user — it asks you before what cannot be undone.
//           automatic — what the build profile says.
//   record: off, errors, actions, interactions, full (what input did: `ui.explain`).
//   designer: whether Live Design is linked; a release leaves it out at compile time.
//   generation: where interface generated while it runs may go (a view's slots) and how
//           open it is — e.g. { surfaces: { extras: "cards an agent adds" },
//           open: { release: "data" } }; without surfaces, none.
import { defineConfig } from "omui/define";

export default defineConfig({
  agent: { development: "development", release: "dual-user" },
  record: { development: "full", release: "errors" },
  designer: { development: true, release: false },
});
```

开发模式（`omui dev`）下 Agent 拥有全部控制权，是否先问你由它结合你的指示判断。发布后的范围是“开发者允许的”与“用户设置的”的交集。

## 问“发生了什么”

框架记下每一次输入和它引起的结果，Agent 可以直接查，不用你转述：

```sh
omui agent timeline <app>                             # 输入做了什么：点中了谁、被什么挡住、触发了哪些动作
omui agent explain <app> --cause 12                   # 某一次按下，从按下到抬起的来龙去脉
omui agent drew <app> '[120,48]'                      # 这个像素是哪个节点画的、在源码哪一行
```

读取人的操作记录在发布后需要人同意。

## 让 Agent 设计界面

`omui dev` 运行时，Agent 可以直接改正在运行的界面：发 TypeScript 或结构化的修改，一帧之内生效，源码是唯一的事实。你随时可以接手；每一步都在历史里，能提交、分支、回退。完整的一次过程见[示例 13](../../../examples/13-live-design)。

## 出了问题怎么办

- **动作被拒绝**：每个拒绝都带原因和出路——节点接受的动作、值要保持的范围、会话缺少的权限。开发模式（`omui dev`）下可信会话拥有全部已声明的能力；在它之外，Agent 能做的是开发者允许、用户配置保留的部分（`omui.config.ts`、`--agent-mode`）。
- **发生了什么、为什么**：`ui.timeline` 列出记录下的交互，`ui.explain` 给出从输入到效果的因果链；问它们，而不是靠人的转述。
- **没有可连接的**：应用要在运行；`omui agent ls` 列出活着的端点。
