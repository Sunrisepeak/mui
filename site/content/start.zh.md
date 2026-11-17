## 从零开始，运行一个界面

通过 xlings 安装构建工具，从仓库构建 `omui` 命令（还没有发布版），再创建应用并运行它：

```sh
xlings install mcpp
git clone --recursive https://github.com/Sunrisepeak/mui && cd mui
mcpp build -p omui.cli
omui new hello && cd hello
omui dev
```

下面是同一个入门程序，正在它的 C++ 源码旁运行。输入名字，再按下按钮。

omui 是具体实现，[openmui](https://github.com/openlifa/openmui) 是开放的 UI 规范。包和模块名是 `omui.*`；属于规范的部分沿用规范的名字（脚本里的 `import openmui`、C ABI、协议）。

接着阅读[核心概念](features.md)或[实践指南](best-practices.md)。
