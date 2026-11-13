# 动效

动画由 hanim 负责：界面动效、代码生成的动画、视频、游戏动画用的是同一个引擎。它的三个特点决定了怎么用：

- **动画是时间的函数。** 任意时刻都能直接算出来，所以可以跳到任何一帧、倒着放、逐位重现。
- **可以被打断。** 动到一半改主意，从当前的位置和速度接着走，不会跳。
- **是数据。** 一段动画是一份可以检查、采样、渲染的文档，人和 Agent 都能写。

## 在界面里：你不写动画

组件只画状态。按钮被按下、面板出现、列表重排时怎么动，由主题和风格包里的配方决定：

```ts
omui.theme("dark");                    // 默认的动效
omui.theme("dark", "reduced_motion");  // 减弱动效：用户的系统设置也会带上这一层
```

想换一种“手感”，换风格包或在主题里重设动效令牌，而不是去改组件：

```ts
motion: {
  "motion.fast": { duration: 120, easing: "cubic-bezier(0.2, 0, 0, 1)" },
  "motion.spring.snappy": { stiffness: 400, damping: 32 },
}
```

这样所有组件的动效保持一致，减弱动效、性能不足时的降级也自动成立。

## 单独做一段动画：一份文档

```json
{
  "version": 1, "size": [480, 320], "background": "#111827",
  "objects": [
    { "id": "toast", "shape": { "rect": { "size": [300, 56], "radius": 14 } }, "fill": "#1f2937", "at": [240, 260] },
    { "id": "icon",  "shape": { "circle": 12 }, "fill": "#34d399", "at": [118, 260] }
  ],
  "program": { "do": "sequence", "items": [
    { "do": "parallel", "items": [
      { "do": "rise_in", "object": "toast", "distance": 40, "duration": 0.4 },
      { "do": "rise_in", "object": "icon",  "distance": 40, "duration": 0.4 } ] },
    { "do": "pulse", "object": "icon", "scale": 1.25 },
    { "do": "wait", "duration": 1.6 },
    { "do": "sink_out", "object": "toast", "distance": 20 }
  ] }
}
```

```sh
hanim check  toast.animation.omui.json                       # 时长、轨道、标记；或每一处问题和它的位置
hanim sample toast.animation.omui.json --at 0.2 --at 1       # 这些时刻每个对象在哪、多大、什么颜色
hanim render toast.animation.omui.json --out toast --frames 16 --webp 30   # 帧、长条图、动图
```

- 步骤可以组合：`sequence`、`parallel`、`stagger`、`loop`、`wait`。
- 现成的配方有四十个：入场（`fade_in`、`rise_in`、`pop_in`……）、离场、强调（`pulse`、`shake`、`tada`……）、待机（`bob`、`breathe`……）、移动（`arc_to`、`swap`、`reorder`）。
- [二十份可以直接拿走的文档](../../../modules/projects/hanim/studio/examples/README.md)：加载、提示、成功的对勾、点赞、卡片洗牌、页面转场、彩纸……

## 在 C++ 里

```cpp
using namespace std::chrono_literals;
auto trip = hanim::sequence(
    hanim::anim::move_to(card, {120, 40}).duration(300ms),
    hanim::anim::rotate_by(card, .12));
auto playback = engine.play(trip);      // 之后可以暂停、跳转、改目标
```

更多：[hanim 的 README](../../../modules/projects/hanim/README.md)。动画视频的完整例子：[宣传片](../../../showcase/promo-video)。
