# openmui（暂时放在私有仓库）

一套开放的 UI 规范，加上一个用 C++ Modules 写的参考实现：**Agent 原生、可以 headless 测试的现代 C++ UI**。

- 一个语义层：像素、无障碍、agent、测试都来自同一棵语义树
- 立即模式和保留模式产出同一棵树；视图可以用原生代码（C++ / TS / Python / Lua）写，也可以用可选的 `.omui` 视图语言写
- 渲染和宿主可以替换：CPU 参考后端、SDL_GPU 等；可以跨平台，也可以跨框架
- 组件契约优先：C++ 实现，TS/JS、Python、Lua 的绑定自动生成
- 由 `mcpp.toml` 驱动，构建、测试、打包都使用 mcpp，分发走 xlings

状态：**设计阶段**。设计文档见 [.agents/docs](.agents/docs/README.md)。
