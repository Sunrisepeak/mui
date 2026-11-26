# 开始

十分钟：工具、一个应用、把它跑起来。

## 1. 工具

构建用 [mcpp](https://github.com/mcpp-community/mcpp)，经 xlings 安装；编译器等由项目钉住，第一次构建时取回。

```sh
xlings install mcpp
```

## 2. 一个项目，跑起来

```sh
omui new hello
cd hello
omui dev
```

窗口开着的时候改 `app/main.ts` 并保存：界面随之改变，计数保留。

## 3. 日常的命令

| 命令 | 做什么 |
|:---|:---|
| `omui dev` | 以开发模式运行（`--headless` 不开窗口） |
| `omui run` | 按发布的样子运行 |
| `omui test` | 运行测试 |
| `omui build` | 构建 |

回到[首页](index.md)。
