# 物质世界

一个 2D 的世界，里面的东西会燃烧、流动、沸腾、凝固、被炸开——而你一条这样的规则都不用写。

思路只有一句：**材料是一组性质，规律只读性质。** 火不认识木头：它把温度升高，凡是有燃料、燃点低于这个温度、旁边有氧气的东西，就烧起来。所以新加一种材料只填它的性质，它就自动参与所有已有的现象。

## 定义物质

标准定义自带 17 种材料（空气、水、冰、蒸汽、木、灰、石、沙、油、岩浆、火药、铁……）和 19 条规律，可以直接用。自己的定义是一份数据，作为包叠加在它上面——甚至可以是世界原本没有的规律，比如这里的磁：

<!-- code: modules/projects/xengine/kernel/matter/packs/magnetism.matter.xdef.ts -->
```ts
// A law the world did not have (xengine.matter; the foundation plan's §6.15, D-181, D-182): magnetism, as a pack
// laid over the standard definition. A lodestone gives a field; the field spreads through everything and fades;
// what is magnetic is drawn up its slope — filings fly to the stone, a crate of iron slides to it, a crate of
// wood stays. No kernel, carrier or work knows the word: it is a channel, two properties and four laws.
//
// Its JSON snapshot, magnetism.matter.xdef.json, is what the kernel reads (matter::read_pack):
//   xdef snapshot …/packs/magnetism.matter.xdef.ts --write
import { defineMatter } from "omui/define";

export default defineMatter({
  columns: {
    gives_field: { unit: "field", doc: "the field it gives, itself; 0: none", most: 1000000 },
    magnetic: { doc: "how strongly a field draws it; 0: not at all", most: 16 },
  },
  channels: {
    field: { unit: "field", doc: "what a lodestone gives: it spreads through everything, and fades" },
  },
  materials: {
    lodestone: { density: 5000, friction: 0.6, restitution: 0.1, shape_stiffness: 1, thermal: 2, gives_field: 400000 },
    filings: { phase: "granular", density: 3000, friction: 0.6, restitution: 0.05, shape_stiffness: 0, thermal: 4, conduct: 4, magnetic: 4 },
    iron: { magnetic: 1 },
  },
  laws: [
    { name: "a lodestone's field", when: "gives_field > 0 && field < gives_field", set: { field: "gives_field" } },
    { name: "field spreads", transfer: "field", multiply: 12, shift: 6 },
    { name: "field fades", when: "field > 0", take: { field: "field / 512" } },
    // Up the slope: toward where there is more of it. Loose things fly; a body's faces are drawn.
    { name: "drawn by a field", move: { up: "field", by: "magnetic", from: 40, speed: 0.0001, force: 2 } },
  ],
});
```

## 一个世界

世界是一张 0.1 米一格的格点，加上由格子组成的刚体和飞在空中的格子。下面出自物质套件的测试（其中的 `material` 取标准定义里的一种材料）：

<!-- code: modules/projects/xengine/kernel/matter/cts/tests/cts/ladder_test.cpp#cavern -->
```cpp
// A cavern with a fire of its own: a floor, a post of wood alight at its foot, a crate afloat in a pool.
std::unique_ptr<bodies::MatterWorld> cavern() {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = {96, 56}, .tile = {16, 16}, .seed = 5, .fill = "air"});
    world->place({0, 50, 96, 6}, material("stone"));
    world->place({20, 36, 3, 14}, material("wood"));
    world->set(*standard().channel("temperature"), {20, 48, 3, 2}, 1000 * 256);
    world->place({56, 42, 2, 8}, material("stone")), world->place({88, 42, 2, 8}, material("stone"));
    world->place({58, 44, 30, 6}, material("water"));
    world->place({68, 38, 5, 4}, material("wood"));
    (void)world->carve({68, 38, 5, 4});
    return world;
}
```

木桩一节一节烧上去变成灰；箱子掉进水里会浮起来，烧掉一半会裂成两块。

游戏的关卡是数据：`defineWorld`（`omui/define`）由图例（一格一个字符，每个字符是一种材料，或者游戏赋予它的角色）和若干行组成，行可以由模块用代码画出来；它求值成游戏读取的 JSON。Kiln 的洞穴就是这样写的（`showcase/kiln/levels/`；图例与画笔在 `cavern.ts`）：

<!-- code: showcase/kiln/levels/first.world.xdef.ts -->
```ts
// The first cavern: a barricade of wood to burn, a pool with a crate afloat, a stone wall to blast, lava to cool
// into a floor, the exit beyond. Two bombs.
import { defineWorld } from "omui/define";
import { firstCavern, legend } from "./cavern";

export default defineWorld({
  name: "the first cavern",
  legend,
  rows: firstCavern(false),
  // Dry roots hanging by the barricade, brush on the ledge before it, a trail of it on the far ledge.
  scenery: [
    { at: [28, 18, 5, 6], material: "wood" },
    { at: [17, 28, 7, 2], material: "wood" },
    { at: [62, 33, 8, 1], material: "wood" },
  ],
  numbers: { bombs: 2 },
});
```

## 回放、分叉、同样的结果

世界是一个确定的值；下面出自物质套件的测试（`make` 搭出一个有刚体、水和着火木头的场景）：

<!-- code: modules/projects/xengine/kernel/matter/cts/tests/cts/coupling_test.cpp#branch -->
```cpp
auto world = make(true);
for (int frame = 0; frame < 60; ++frame) world->step();
const auto saved = world->save();
auto branch = world->branch();
expect(fatal(branch.has_value()));
std::vector<std::uint64_t> ahead;
for (int frame = 0; frame < 120; ++frame) world->step(), ahead.push_back(world->state_hash());
expect(fatal(world->restore(saved).has_value()));
expect(eq(world->frames(), std::uint64_t{60}));
bool again = true, branched = true;
for (std::size_t frame = 0; frame < 120; ++frame) {
    world->step();
    (*branch)->step();
    again = again && world->state_hash() == ahead[frame];
    branched = branched && (*branch)->state_hash() == ahead[frame];
}
expect(again) << "restored and run again: the same bits frame by frame";
expect(branched) << "the branch ran on as the original did";
```

这是回滚、撤销、“如果这样会怎样”、Agent 试玩的基础。

## 上屏，按机器的能力

画一个世界：画面读取世界，再把显示列表交给 CPU 参考渲染器（出自阶梯的测试）：

<!-- code: modules/projects/xengine/kernel/matter/cts/tests/cts/ladder_test.cpp#shown -->
```cpp
struct Shown {
    xrender::ImageStore images;
    ca::WorldPicture picture;
    xrender::DisplayList list;
    xrender::CpuRenderer renderer;
    explicit Shown(ca::Light best = ca::Light::glow, bool alive = true)
        : picture(*ca::WorldPicture::make(images, standard(), look(), {96, 56}, {.fill = "air", .seed = 9, .best_light = best, .scenery_alive = alive})) {
        picture.scenery({23, 46, 10, 4}, material("wood"));  // a hedge beside the post
        picture.scenery({40, 49, 12, 1}, material("wood"));  // and dry grass along the floor
    }
    raster::Surface frame(const bodies::MatterWorld& world) {
        picture.show(world);
        const float width = 96.0f * scale, height = 56.0f * scale;
        list.clear();
        list.fill_rect({0, 0, width, height}, geom::Color::hex(0x0d1117));
        picture.draw(list, {0, 0, width, height});
        raster::Surface out(96 * scale, 56 * scale);
        (void)renderer.render(list, out, {&images, nullptr});
        return out;
    }
};
```

画面有两条可以自动降档的阶梯：光（辉光 → 预先烘焙的火焰帧 → 无）和布景（跟着世界一起模拟 → 静止）。机器跑不动时降档，玩法逐位不变。

## 不写代码先试：材料实验台

```sh
matter-lab new lab --shape 64x32
matter-lab run lab            # 一行一个请求
```

实验台的测试回放的一段会话，画面存在它旁边（`media/`）：

<!-- code: modules/projects/xengine/kernel/matter/lab/media/coal-beside-wood.jsonl -->
```jsonl
{"op":"matter.place","material":"stone","region":[0,29,64,3]}
{"op":"matter.place","material":"stone","region":[30,22,2,7]}
{"op":"matter.place","material":"wood","region":[4,27,22,2]}
{"op":"matter.apply","channel":"temperature","set":900,"region":[4,27,2,1]}
{"op":"lab.commit","message":"a hearth of wood, a wall beside it"}
{"op":"lab.material","name":"coal","properties":{"density":1400,"thermal":1,"ignition":700,"fuel":900,"heat":120,"burnt":"ash","look":{"color":"#4a4540","grain":0.14}},"intent":"a 13th material: coal — harder to light than wood, burns longer and hotter"}
{"op":"matter.place","material":"coal","region":[36,27,22,2]}
{"op":"matter.apply","channel":"temperature","set":650,"region":[36,27,2,1]}
{"op":"field.step","steps":200}
{"op":"matter.explain","cell":[36,27],"law":"burns"}
{"op":"matter.apply","channel":"temperature","set":1100,"region":[36,27,2,1]}
{"op":"field.step","steps":400}
{"op":"matter.explain","cell":[39,27],"depth":1}
{"op":"lab.commit","message":"coal beside wood: lit at 1100 K, not at 650"}
{"op":"lab.try","material":"coal","properties":{"ignition":560}}
{"op":"lab.explain","part":"materials/coal","property":"ignition"}
{"op":"lab.picture","scale":8}
```

`matter.explain` 问“这一格为什么是现在这样”，回答是哪条规律成立、它读到了什么、热是谁给的。加一种新材料是一条 `lab.material`（这里是煤），改完世界立刻重放。

更多：[物质](../../../modules/projects/xengine/kernel/matter/README.md)、[格点与世界](../../../modules/projects/xengine/kernel/cellular/README.md)、[实验台](../../../modules/projects/xengine/kernel/matter/lab/README.md)。现在只有 2D。

## 出了问题怎么办

- **规则点名了材料**：规则只引用性质，不点名材料（lint `matter`）；给材料加上这个性质，让规则去读它。
- **分支和原来的不一致**：世界是值，按步确定地推进；像上面的测试那样逐帧比较 `state_hash()`，找到第一次分开的那一帧。
- **机器慢**：画面会自己降低光照和布景两条阶梯；它画的世界逐位不变。
