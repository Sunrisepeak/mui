# A world of matter

A world in two dimensions whose things burn, flow, boil, set and are blown apart — and you write not one rule saying so.

The idea is one sentence: **a material is a set of properties, and a law reads properties only.** Fire does not know wood: it raises a temperature, and whatever has fuel, an ignition point below that temperature and oxygen beside it burns. So a new material says its properties and takes part in everything there already is.

## Defining matter

The standard definition has 17 materials (air, water, ice, steam, wood, ash, stone, sand, oil, lava, gunpowder, iron …) and 19 laws, ready to use. Your own is data, laid over it as a pack — even a law the world did not have, as magnetism here:

<!-- code: modules/projects/meng/kernel/matter/packs/magnetism.matter.xdef.ts -->
```ts
// A law the world did not have (meng.matter; the foundation plan's §6.15, D-181, D-182): magnetism, as a pack
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

## A world

A world is a lattice of cells 0.1 m across, with rigid bodies made of its cells and cells in flight. From the matter suite's tests (`material` there names a material of the standard definition):

<!-- code: modules/projects/meng/kernel/matter/cts/tests/cts/ladder_test.cpp#cavern -->
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

The post burns upward piece by piece and leaves ash; the crate floats if it falls in water, and falls in two when half of it has burned.

A game's level is data: `defineWorld` (`omui/define`) is a legend — a character a cell, each a material or a role the game gives it — and rows, which a module may paint by code; it evaluates to JSON the game reads. Kiln's caverns are written so (`showcase/kiln/levels/`; the legend and the painter are in `cavern.ts`):

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

## Replay, branches, the same result

A world is a deterministic value; from the matter suite's tests (`make` builds a scene of bodies, water and wood alight):

<!-- code: modules/projects/meng/kernel/matter/cts/tests/cts/coupling_test.cpp#branch -->
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

Rollback, undo, "what if", and an agent's play-testing stand on this.

## On screen, as the machine can

A world drawn: a picture that reads it, then a display list drawn by the CPU reference renderer (from the ladders' test):

<!-- code: modules/projects/meng/kernel/matter/cts/tests/cts/ladder_test.cpp#shown -->
```cpp
struct Shown {
    mrender::ImageStore images;
    ca::WorldPicture picture;
    mrender::DisplayList list;
    mrender::CpuRenderer renderer;
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

The picture has two ladders that are lowered by themselves: the light (a glow → frames of a flame baked beforehand → none) and the scenery (simulated beside the world → still). A slow machine lowers them, and the game is the same bit for bit.

## Try it before writing code: the material lab

```sh
matter-lab new lab --shape 64x32
matter-lab run lab            # a request a line
```

A session the lab's tests replay, its picture kept beside it (`media/`):

<!-- code: modules/projects/meng/kernel/matter/lab/media/coal-beside-wood.jsonl -->
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

`matter.explain` asks why a cell is what it is; the answer is the law that held, what it read, and who gave it its heat. A new material is one `lab.material` (coal, here), and the world is played again at once.

More: [matter](../../../modules/projects/meng/kernel/matter/README.md), [the lattice and the world](../../../modules/projects/meng/kernel/cellular/README.md), [the lab](../../../modules/projects/meng/kernel/matter/lab/README.md). Two dimensions only, today.

## When something goes wrong

- **A rule names a material**: rules refer to properties, never to materials by name (lint `matter`); give the material the property and let the rule read it.
- **A branch drifts from its original**: a world is a value and steps deterministically; compare `state_hash()` frame by frame, as the test above does, to find the first frame where they part.
- **A slow machine**: the picture lowers its light and scenery ladders by itself; the world it draws is the same bit for bit.
