# A world of matter

A world in two dimensions whose things burn, flow, boil, set and are blown apart — and you write not one rule saying so.

The idea is one sentence: **a material is a set of properties, and a law reads properties only.** Fire does not know wood: it raises a temperature, and whatever has fuel, an ignition point below that temperature and oxygen beside it burns. So a new material says its properties and takes part in everything there already is.

## Defining matter

The standard definition has 17 materials (air, water, ice, steam, wood, ash, stone, sand, oil, lava, gunpowder, iron …) and 19 laws, ready to use. Your own is data:

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
    { transfer: "temperature", by: "thermal" },                                   // heat is conducted
    { when: "temperature >= ignition && burned < fuel && near.oxygen > 0",
      add: { burned: "burn_rate" }, flag: "burning" },                            // burning
    { when: "phase == liquid && boiling > 0 && temperature >= boiling", become: "boiled" },
  ],
});
```

## A world

A world is a lattice of cells 0.1 m across, with rigid bodies made of its cells and cells in flight.

```cpp
import xengine.matter.json;
import xengine.cellular.rigid;
namespace world2d = xengine::cellular::rigid;

const auto program = *xengine::matter::standard();
auto world = *world2d::MatterWorld::make(program, {.shape = {128, 64}, .fill = "air"});

world->place({0, 56, 128, 8}, *program.material("stone"));       // the ground
world->place({30, 40, 4, 16}, *program.material("wood"));        // a post
world->set(*program.channel("temperature"), {30, 54, 4, 2}, 1000 * 256);   // lit at its foot (1000 K)
world->place({60, 50, 6, 4}, *program.material("wood"));
(void)world->carve({60, 50, 6, 4});                              // this wood is a crate that moves

for (int frame = 0; frame < 600; ++frame) world->step();        // 60 frames a second
```

The post burns upward piece by piece and leaves ash; the crate floats if it falls in water, and falls in two when half of it has burned.

## Replay, branches, the same result

A world is a deterministic value:

```cpp
auto saved  = world->save();
auto future = *world->branch();           // a copy of its own
for (int i = 0; i < 120; ++i) future->step();   // two seconds ahead; the world itself has not moved
(void)world->restore(saved);              // back to where it was saved
world->state_hash();                      // the same steps: the same number on every machine
```

Rollback, undo, "what if", and an agent's play-testing stand on this.

## On screen, as the machine can

```cpp
import xengine.cellular.present.world;
auto picture = *xengine::cellular::WorldPicture::make(images, program, look, {128, 64}, {.fill = "air"});
picture.show(*world);            // reads the world, and only reads
picture.draw(list, {0, 0, 768, 384});
```

The picture has two ladders that are lowered by themselves: the light (a glow → frames of a flame baked beforehand → none) and the scenery (simulated beside the world → still). A slow machine lowers them, and the game is the same bit for bit.

## Try it before writing code: the material lab

```sh
matter-lab new lab --shape 64x32
matter-lab run lab            # a request a line
```

```jsonl
{"op":"matter.place","material":"wood","region":[4,27,22,2]}
{"op":"matter.apply","channel":"temperature","set":900,"region":[4,27,2,1]}
{"op":"field.step","steps":200}
{"op":"matter.explain","cell":[10,27]}
```

The last asks why a cell is what it is; the answer is the law that held, what it read, and who gave it its heat. A new material is one `lab.material`, and the world is played again at once.

More: [matter](../../../modules/projects/xengine/kernel/matter/README.md), [the lattice and the world](../../../modules/projects/xengine/kernel/cellular/README.md), [the lab](../../../modules/projects/xengine/kernel/matter/lab/README.md). Two dimensions only, today.
