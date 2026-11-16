# 动效

动画由 hanim 负责：界面动效、代码生成的动画、视频、游戏动画用的是同一个引擎。它的三个特点决定了怎么用：

- **动画是时间的函数。** 任意时刻都能直接算出来，所以可以跳到任何一帧、倒着放、逐位重现。
- **可以被打断。** 动到一半改主意，从当前的位置和速度接着走，不会跳。
- **是数据。** 一段动画是一份可以检查、采样、渲染的文档，人和 Agent 都能写。

## 在界面里：你不写动画

组件只画状态。按钮被按下、面板出现、列表重排时怎么动，由主题和风格包里的配方决定。应用只做选择；[例子 06](../../../examples/06-theming) 叠加一个主题和它的覆盖层，其中有减弱动效（用户的系统设置也会加上这一层）：

<!-- code: examples/06-theming/logic/main.ts#compose -->
```ts
// The composition the controls say: a theme, then overlays.
function compose(): void {
  const layers: string[] = [scheme.get()];
  if (compact.get()) layers.push("compact");
  if (calm.get()) layers.push("reduced_motion");
  try {
    theme(layers);
    problem.set("");
  } catch (error) {
    problem.set(String((error as Error).message));
  }
  current.set(themes().current);
}
```

想换一种“手感”，换风格包或在主题里重设动效令牌，而不是去改组件。omui 默认主题里的减弱动效覆盖层就是这样一处改动，把每个动效令牌都换成瞬时的：

<!-- code: modules/projects/omui/theme/packs/default/default.theme.xdef.ts#reduced-motion -->
```ts
// The person asked for less motion (spec/registry/motion.json, reduced_motion):
// every transition shows its target at once, springs settle at once.
reduced_motion: {
  doc: "Reduced motion: transitions show their targets at once.",
  overlay: true,

  motion: {
    "motion.fast": "motion.instant",
    "motion.normal": "motion.instant",
    "motion.slow": "motion.instant",
    "motion.enter": "motion.instant",
    "motion.exit": "motion.instant",
    "motion.spring.snappy": "motion.instant",
    "motion.spring.gentle": "motion.instant",
    "motion.spring.bouncy": "motion.instant",
  },
},
```

这样所有组件的动效保持一致，减弱动效、性能不足时的降级也自动成立。

## 单独做一段动画：一份文档

hanim studio 的文档之一，一个提示条：

<!-- code: modules/projects/hanim/studio/examples/toast.animation.xdef.json -->
```json
{
  "version": 1,
  "size": [480, 320],
  "background": "#111827",
  "objects": [
    { "id": "toast", "shape": { "rect": { "size": [300, 56], "radius": 14 } }, "fill": "#1f2937", "stroke": { "color": "#374151", "width": 1 }, "at": [240, 260] },
    { "id": "icon", "shape": { "circle": 12 }, "fill": "#34d399", "at": [118, 260] },
    { "id": "line", "shape": { "rect": { "size": [150, 10], "radius": 5 } }, "fill": "#e5e7eb", "at": [225, 252] },
    { "id": "subline", "shape": { "rect": { "size": [110, 8], "radius": 4 } }, "fill": "#6b7280", "at": [205, 270] }
  ],
  "program": {
    "do": "sequence",
    "items": [
      { "do": "parallel", "items": [
        { "do": "rise_in", "object": "toast", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "line", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "subline", "distance": 40, "duration": 0.4 },
        { "do": "rise_in", "object": "icon", "distance": 40, "duration": 0.4 }
      ] },
      { "do": "pulse", "object": "icon", "scale": 1.25 },
      { "do": "marker", "name": "shown" },
      { "do": "wait", "duration": 1.6 },
      { "do": "parallel", "items": [
        { "do": "sink_out", "object": "toast", "distance": 20 },
        { "do": "sink_out", "object": "line", "distance": 20 },
        { "do": "sink_out", "object": "subline", "distance": 20 },
        { "do": "sink_out", "object": "icon", "distance": 20 }
      ] }
    ]
  }
}
```

```sh
hanim document.check  --file toast.animation.xdef.json                       # 时长、轨道、标记；或每一处问题和它的位置
hanim document.sample --file toast.animation.xdef.json --at 0.2 --at 1       # 这些时刻每个对象在哪、多大、什么颜色
hanim document.render --file toast.animation.xdef.json --out toast --frames 16 --webp 30   # 帧、长条图、动图
```

- 步骤可以组合：`sequence`、`parallel`、`stagger`、`loop`、`wait`。
- 现成的配方有四十个：入场（`fade_in`、`rise_in`、`pop_in`……）、离场、强调（`pulse`、`shake`、`tada`……）、待机（`bob`、`breathe`……）、移动（`arc_to`、`swap`、`reorder`）；[每个配方的过程采样图](../../../modules/projects/hanim/RECIPES.md)。
- [二十份可以直接拿走的文档](../../../modules/projects/hanim/studio/examples/README.md)：加载、提示、成功的对勾、点赞、卡片洗牌、页面转场、彩纸……

## 在 C++ 里

出自 hanim 的配方测试：两个对象交换位置——播放、暂停、跳转，并读出每个时刻（那里的 `take` 取出结果）：

<!-- code: modules/projects/hanim/tests/program/recipes_test.cpp#swap -->
```cpp
// Two objects.
Engine engine = take(Engine::make());
const auto a = take(engine.create_object(square({100, 100}))), b = take(engine.create_object(square({300, 100})));
const auto played = take(engine.play(anim::swap(a, b, {200, 0})));
take(engine.pause(played));
const double length = take(engine.inspect(played)).duration.count();
take(engine.seek(played, seconds(length / 2)));
const double ya = take(engine.frame(played, 0)).geometry.parts[0].local.ty, yb = take(engine.frame(played, 1)).geometry.parts[0].local.ty;
expect((ya - 100) * (yb - 100) < 0) << std::format("they pass on either side: {} and {}", ya, yb);
take(engine.seek(played, seconds(length)));
expect(near(take(engine.frame(played, 0)).geometry.parts[0].local.tx, 300) && near(take(engine.frame(played, 1)).geometry.parts[0].local.tx, 100)) << "and exchange places";
```

更多：[hanim 的 README](../../../modules/projects/hanim/README.md)。动画视频的完整例子：[宣传片](../../../showcase/promo-video)。

## 出了问题怎么办

- **变化是跳过去的而不是动过去的**：节点的 `motion` 槽（或主题里对应的配方）没有给这个变化一条路线；组件自己从不写动画（lint `motion`）。
- **某一帧和预期不同**：动画是时间的函数；在相同的时刻采样（测试用显式的时间推进运行时），各个平台上都一样。
- **打断时跳了一下**：被打断的过程从当前画出的位置继续；跳动说明这次变化替换了节点而不是更新它，保持它的 `id`。
