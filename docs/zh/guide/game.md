# 游戏

> 早期：能做出可以玩、可以回放、Agent 能玩的 2D 游戏；窗口里的整套流程、场景与关卡工具还没有。

一个游戏在这里是三样东西：**状态**（一份可以整个复制的数据）、**一步**（状态加输入得到下一个状态）、**给玩家看的东西**。它不依赖界面框架，不开窗口也能跑——在服务器上、在一千次自动试玩里、在 CI 里。

## 写一个游戏

```cpp
import xengine.runtime;

struct Play {
    using State = …;  using Input = …;                        // 普通数据，整个复制
    static void step(State&, const Input&);                   // 一步：纯函数
    static constexpr std::int32_t steps_per_second = 60;
    static std::uint64_t checksum(const State&);              // 同样的玩法，同样的数
    static std::string   observe(const State&, bool full);    // 玩家看到的，JSON
    static void          play(Input&, const xengine::Actions&);  // 把按键、摇杆、指针变成这一步的输入
    static bool          over(const State&);
};

xengine::runtime::Runtime<Play> game(initial);
```

写成这样，下面这些就都有了，不用另做：

- **回放与倒带**：每一步的输入被记下，任何一帧都能回去。
- **分叉**：复制状态往前试，不影响正在玩的这一局。
- **确定**：同样的输入在任何平台上是同样的结果，可以钉在测试里。

## Agent 来玩

游戏自动是一个 Agent 能操作的对象，操作只有六个：

| 操作 | 意思 |
|---|---|
| `world.observe` | 看：这个档位的玩家被展示的东西 |
| `world.act` | 做：和人一样的按键、摇杆、指针 |
| `world.step` | 走几步 |
| `world.seek` | 回到某一帧 |
| `world.branch` | 如果这样做，几步之后会怎样（游戏本身不动） |
| `world.state` | 第几帧、状态的哈希、结束了没有 |

Agent 以什么身份玩由你决定：**人的档位**只看得到人看得到的，有反应时间和手速上限；**辅助档位**能看到全部。这样 Agent 可以和人公平地玩，也可以当试玩员。

## 用物质世界做游戏

把[物质世界](world.md)放进状态里（它是一个值，复制即分叉），游戏只需要加上自己的那一点：角色怎么读世界、道具怎么写世界、怎样算赢。

[Kiln](../../../showcase/kiln) 是这样做的一个小游戏：像素猫 Mui 拿着火、水、炸弹三种法术过洞穴，没有一处是脚本——木障被火烧开，箱子浮在水上，石墙被炸开，水把岩浆浇成石头。它的测试里有一个只用上面六个操作的试玩员，能通关，也能在一个故意做坏的关卡里停下来并说出卡在哪。

[组件打砖块](../../../showcase/widget-breakout)是另一个：砖块是真的界面组件，一个 Agent 副驾和你一起玩。

## 有窗口的游戏

窗口、输入、界面归 omui；游戏和界面在一座桥（`xengine.ui`）上相接：游戏照常走自己的步，界面读它的状态来画。HUD、菜单、设置就是普通的 omui 界面。
