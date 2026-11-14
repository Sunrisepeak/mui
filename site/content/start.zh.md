## 从零开始，运行一个界面

通过 xlings 安装构建工具，创建应用，再运行它：

```sh
xlings install mcpp
mcpp new hello --template openmui
cd hello
mcpp run
```

下面是同一个入门程序，正在它的 C++ 源码旁运行。输入名字，再按下按钮。

omui 是具体实现，[openmui](https://github.com/openlifa/openmui) 是开放的 UI 规范。现有包和模块名继续使用 `openmui.*`。

接着阅读[核心概念](features.md)或[实践指南](best-practices.md)。
