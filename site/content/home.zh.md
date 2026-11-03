## 一棵树，多种呈现

omui 是实现 openmui 开放规范的 UI 框架，建立在一个想法上：**界面是一棵语义树**，其他一切都是它的呈现。

- 你看到的**像素**是一种呈现。能用 GPU 时由你的 GPU 绘制；CPU 绘制的是参考像素，每个平台上逐位相同。顶栏的帧率表显示由谁绘制、有多快。
- **辅助技术**读的是另一种：树上的角色、名字和状态。
- **agent** 把这棵树当数据读，用和人的输入同一组动作操作它，经过同样的检查，可以同样地撤销。
- **测试**对这棵树断言，并固定像素。

这个网站本身用 omui 做成。切换上方的**镜头**，可以看到你正在读的这一页的每一种呈现：像素、语义树的线框、agent 收到的 JSON、测试会写下的断言。

## 用它来做

1. 安装 [xlings](https://github.com/openxlings/xlings)，再装构建工具：`xlings install mcpp`。
2. 新建一个应用并运行：`mcpp new hello --template openmui && cd hello && mcpp run`。
3. 让 agent 像人一样按下它的按钮：`omui agent act org.example.hello main/click activate`。

这个网站上的**示例 01** 就是这样一个程序，旁边是它自己的源码。[示例](https://github.com/Sunrisepeak/mui/tree/openmui-design/examples)从这里接着往下：表单、文档、画布，一个应用里的四种语言。

## 这里有什么

- **特性**和**实践**：框架的文档，中英两种语言。可以先读[omui 的不同之处](features.md)。
- **时间线**：一个真正可用的时间线编辑器。拖动片段、拖边修剪、按 S 在播放头处切开，再按 Ctrl+Z。每次编辑都是一个精确的修改，带着它精确的反向。

- **这一页上的 agent**：请它读这一页、编辑时间线或打开一个链接。每一步都是总线上真实的请求，由 [openas](https://github.com/openlifa/openas) 决定。外部的动作要先问你，回答的按钮只有你能按。

规范是一份开放标准：[openlifa/openmui](https://github.com/openlifa/openmui)（CC BY-SA 4.0）。

页脚是这一页的时间：往回拖，页面就画成当时的样子——镜头的飞行、切换语言时的字母、镜头切换的淡入，都能倒着放。动画是时间的函数，所以任何一刻都能重新画出来；回去的只是画面，语义树仍是现在的。旁边是你的浏览器算出的语义树哈希。确定性是设计的一部分：每台机器上都是同一棵树。
