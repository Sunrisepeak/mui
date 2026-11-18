# 游戏

> 预览：能做出可以玩、可以回放、Agent 能玩的 2D 游戏；窗口里的整套流程、场景与关卡工具还没有。

一个游戏在这里是三样东西：**状态**（一份可以整个复制的数据）、**一步**（状态加输入得到下一个状态）、**给玩家看的东西**。它不依赖界面框架，不开窗口也能跑——在服务器上、在一千次自动试玩里、在 CI 里。

## 写一个游戏

一个游戏是一个类型：它的状态、输入、一步、给玩家看的东西。下面是运行时自己测试里的一个小游戏，一条线上的追逐：

<!-- code: modules/projects/meng/game/runtime/tests/runtime_test.cpp#chase -->
```cpp
// The player walks a line after a mark that jumps when reached; five catches end it.
struct Chase {
    struct State {
        std::int64_t  frame = 0;
        std::int32_t  player = 0, mark = 40, caught = 0, spare = 0;
        std::uint64_t rng = 0x9e3779b97f4a7c15ull;
    };
    struct Input {
        meng::Actions player{};
    };
    static constexpr std::int32_t steps_per_second = 120;
    static void step(State& state, const Input& input) {
        ++state.frame;
        if (input.player.axis(0) > 0.5) ++state.player;
        if (input.player.axis(0) < -0.5) --state.player;
        if (state.player == state.mark) {
            ++state.caught;
            state.rng = state.rng * 6364136223846793005ull + 1442695040888963407ull;
            state.mark = static_cast<std::int32_t>((state.rng >> 40) % 61) - 30;
        }
    }
    static std::uint64_t checksum(const State& state) noexcept {
        // Field by field (no padding hashed): the same play, the same number.
        const std::array<std::uint64_t, 5> parts{static_cast<std::uint64_t>(state.frame), static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.player)),
                                                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.mark)), static_cast<std::uint64_t>(state.caught),
                                                 state.rng};
        return mlibs::hash::fnv1a(std::as_bytes(std::span(parts)));
    }
    // A player is shown where they and the mark stand; only the full view shows where the mark jumps next.
    static std::string observe(const State& state, bool full) {
        std::string out = std::format("{{\"player\":{},\"mark\":{},\"caught\":{}", state.player, state.mark, state.caught);
        if (full) out += std::format(",\"rng\":\"{:016x}\"", state.rng);
        return out + "}";
    }
    static void play(Input& input, const meng::Actions& actions) { input.player = actions; }
    static bool over(const State& state) noexcept { return state.caught >= 5; }
    // What the game says of itself: what its controls mean, and a task to begin with.
    static constexpr std::string_view about = "A chase on a line: walk onto the mark and it jumps elsewhere; five catches end the game";
    static constexpr std::array<runtime::Control, 2> controls{{{.name = "left", .summary = "Walk toward lower numbers", .axis = 0, .toward = -1},
                                                               {.name = "right", .summary = "Walk toward higher numbers", .axis = 0, .toward = 1}}};
    static constexpr std::array<runtime::Move, 2>    to_the_mark{{{.hold = {"right"}, .steps = 60}, {.steps = 1}}};
    static constexpr std::array<runtime::Task, 1>    tasks{{{"catch-the-first-mark", "Walk right to the first mark (it stands at 40) and stop", to_the_mark}}};
};
```

由 `meng::runtime::Runtime<Chase>`（`import meng.runtime;`）运行。写成这样，下面这些就都有了，不用另做：

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

窗口、输入、界面归 omui；游戏和界面在一座桥（`meng.ui`）上相接：游戏照常走自己的步，界面读它的状态来画。HUD、菜单、设置就是普通的 omui 界面。

## 出了问题怎么办

- **回放不一致**：相同的输入得到相同的结果；`world.state` 给出每一帧的状态哈希，把两次运行并排推进，哈希第一次不同的那一帧就是分开的地方。
- **Agent 玩得太好**：它在辅助档，能看到全部；人类档只看到人看到的，并有人的反应时间和每秒动作数。
