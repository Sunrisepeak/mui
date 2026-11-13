# 物质世界

一个 2D 的世界，里面的东西会燃烧、流动、沸腾、凝固、被炸开——而你一条这样的规则都不用写。

思路只有一句：**材料是一组性质，规律只读性质。** 火不认识木头：它把温度升高，凡是有燃料、燃点低于这个温度、旁边有氧气的东西，就烧起来。所以新加一种材料只填它的性质，它就自动参与所有已有的现象。

## 定义物质

标准定义自带 17 种材料（空气、水、冰、蒸汽、木、灰、石、沙、油、岩浆、火药、铁……）和 19 条规律，可以直接用。自己的定义是一份数据：

```ts
// world.matter.omui.ts
import { defineMatter } from "omui/define";
export default defineMatter({
  materials: {
    air:   { phase: "gas", density: 1.2, oxygen: 1 },
    wood:  { density: 600, ignition: 570, fuel: 400, heat: 80, burnt: "ash" },
    water: { phase: "liquid", density: 1000, boiling: 373, boiled: "steam", frozen: "ice" },
  },
  laws: [
    { transfer: "temperature", by: "thermal" },                                   // 热会传导
    { when: "temperature >= ignition && burned < fuel && near.oxygen > 0",
      add: { burned: "burn_rate" }, flag: "burning" },                            // 燃烧
    { when: "phase == liquid && boiling > 0 && temperature >= boiling", become: "boiled" },
  ],
});
```

## 一个世界

世界是一张 0.1 米一格的格点，加上由格子组成的刚体和飞在空中的格子。

```cpp
import xengine.matter.json;
import xengine.cellular.rigid;
namespace world2d = xengine::cellular::rigid;

const auto program = *xengine::matter::standard();
auto world = *world2d::MatterWorld::make(program, {.shape = {128, 64}, .fill = "air"});

world->place({0, 56, 128, 8}, *program.material("stone"));       // 地面
world->place({30, 40, 4, 16}, *program.material("wood"));        // 一根木桩
world->set(*program.channel("temperature"), {30, 54, 4, 2}, 1000 * 256);   // 点着它的底部（1000 K）
world->place({60, 50, 6, 4}, *program.material("wood"));
(void)world->carve({60, 50, 6, 4});                              // 这块木头成为一个能动的箱子

for (int frame = 0; frame < 600; ++frame) world->step();        // 每秒 60 帧
```

木桩一节一节烧上去变成灰；箱子掉进水里会浮起来，烧掉一半会裂成两块。

## 回放、分叉、同样的结果

世界是一个确定的值：

```cpp
auto saved  = world->save();              // 存
auto future = *world->branch();           // 分叉：一份独立的拷贝
for (int i = 0; i < 120; ++i) future->step();   // 往前试两秒，原来的世界不动
(void)world->restore(saved);              // 回到存的那一刻
world->state_hash();                      // 同样的步骤，在任何机器上是同一个数
```

这是回滚、撤销、“如果这样会怎样”、Agent 试玩的基础。

## 上屏，按机器的能力

```cpp
import xengine.cellular.present.world;
auto picture = *xengine::cellular::WorldPicture::make(images, program, look, {128, 64}, {.fill = "air"});
picture.show(*world);            // 只读世界
picture.draw(list, {0, 0, 768, 384});
```

画面有两条可以自动降档的阶梯：光（辉光 → 预先烘焙的火焰帧 → 无）和布景（跟着世界一起模拟 → 静止）。机器跑不动时降档，玩法逐位不变。

## 不写代码先试：材料实验台

```sh
matter-lab new lab --shape 64x32
matter-lab run lab            # 一行一个请求
```

```jsonl
{"op":"matter.place","material":"wood","region":[4,27,22,2]}
{"op":"matter.apply","channel":"temperature","set":900,"region":[4,27,2,1]}
{"op":"field.step","steps":200}
{"op":"matter.explain","cell":[10,27]}
```

最后一条问“这一格为什么是现在这样”，回答是哪条规律成立、它读到了什么、热是谁给的。加一种新材料是一条 `lab.material`，改完世界立刻重放。

更多：[物质](../../../modules/projects/xengine/kernel/matter/README.md)、[格点与世界](../../../modules/projects/xengine/kernel/cellular/README.md)、[实验台](../../../modules/projects/xengine/kernel/matter/lab/README.md)。现在只有 2D。
