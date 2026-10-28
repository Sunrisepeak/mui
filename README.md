# openmui（暂时放在私有仓库）

一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现：**Agent 原生、可以 headless 测试的现代 C++ UI**。规范是 OpenLifa 的开放标准：[openmui](https://github.com/openlifa/openmui)（本仓库的 `spec/`）和 [openas](https://github.com/openlifa/openas)（actor、效果、授权、信封）。

- 一个语义层：像素、无障碍、agent、测试都来自同一棵语义树
- 立即模式和保留模式产出同一棵树；视图代码优先，直接用 C++ / TS / Python / Lua 写，布局和逻辑在一起；`*.view.omui.json` 是给工具和 agent 用的可选数据格式
- 渲染器只写一次，基于 GPU 抽象层 `omui.rhi`；支持用 Slang 写自定义着色器
- 渲染和宿主可以替换：CPU 参考后端、SDL_GPU 等；可以跨平台，也可以跨框架
- 组件契约优先：C++ 实现，TS/JS、Python、Lua 的绑定自动生成
- 由 `mcpp.toml` 驱动，构建、测试、打包都使用 mcpp，分发走 xlings

## 核心特色（详见 [docs/zh/features.md](docs/zh/features.md) · [English](docs/en/features.md)）

1. **统一语义树**：像素、无障碍、agent、测试读的是同一棵树；节点 ID 稳定，不依赖显示文字
2. **Agent 原生且安全**：每个应用自带 agent 接口和 MCP 服务器；不可逆动作由人确认，“人的动作”任何 agent 都无法冒充；开发者定上限，人按 agent、效果等级和范围授权（openas §5）
3. **确定性**：三个平台、两种编译器，逐字节相同的像素；会话可以精确回放
4. **一棵树，四种语言**：C++ / TS / Python / Lua 和视图文档得到同一棵树，绑定由契约生成
5. **强动画**：动画是时间的纯函数，所有可视属性都能动，粒度到字形和像素
6. **CPU 参考 + GPU 渲染**，Slang 效果是一等公民（热重载约 50ms），风格包换外观不换语义
7. **性能可证明**：预算进 CI，基准报告公开（[docs/zh/benchmarks](docs/zh/benchmarks/README.md)）
8. **简单的写法，深入的控制**：默认就对，需要时可以一路下到 GPU
9. **一切可替换**：核心组件可以单独发布，宿主也是插件
10. **国际化不用 ICU**：引擎和数据分开，结果在各平台一致，en / zh 顶级支持

状态：**实现中**（按[实施计划](.agents/docs/2026-10-26-openmui-implementation-plan.md)）。设计文档见 [.agents/docs](.agents/docs/README.md)。
