# 三维世界

用 omui 的各个引擎做一个三维游戏，以 [Quay](../../../showcase/quay)（预览）为例：守夜人在夜里的港口点亮灯塔，锈蚀的守卫在集装箱之间巡逻。游戏是状态与一步动作的纯函数；画面、声音和窗口只读取状态。

## 刚体与人群

港口是一个三维刚体世界（`xengine.dynamics` 的 rigid3：在引擎契约之下的 Jolt）：集装箱固定，板条箱可动，守夜人是角色。集装箱的三角形同时组成射线与声音所遇的网格；钢板让声音透过 0.15，并把 800 Hz 以上变闷。

<!-- code: showcase/quay/src/sim/sim.cpp#bodies -->
```cpp
auto bodies = rigid3::World::make();
if (!bodies) return state;
for (const Box& box : level.boxes) {
    if (box.crate) {
        const auto crate = bodies->add({.kind = rigid3::Kind::moving, .at = box.at}, rigid3::Shape::box(box.half), {.density = 300, .friction = 0.7f});
        if (crate) state.crates.push_back(*crate);
        continue;
    }
    (void)bodies->add({.kind = rigid3::Kind::fixed, .at = box.at}, rigid3::Shape::box(box.half));
    (void)geom::append(statics->mesh, geom::box(box.half), math::translation(box.at));
}
statics->tree = geom::bvh_of(statics->mesh);
// Steel lets a tenth of a sound by, and dulls it above 800 Hz.
statics->walls.push_back({.mesh = &statics->mesh, .tree = &statics->tree, .place = math::Mat4{}, .through = 0.15f, .bright = 800});
const auto keeper = bodies->add(rigid3::CharacterDef{.at = level.start});
```

守卫走在用同一批三角形建的导航网格上（`xengine.nav`：Detour 的人群），各有巡逻路线：

<!-- code: showcase/quay/src/sim/sim.cpp#crowd -->
```cpp
auto ground = nav::World::build({.points = statics->mesh.positions, .corners = statics->mesh.indices}, {.radius = warden_radius, .height = warden_height});
if (!ground) return state;
for (const Patrol& round : level.wardens) {
    const auto walker = ground->add({.at = round.from, .radius = warden_radius, .height = warden_height, .speed = 2.6f});
    if (!walker) continue;
    (void)ground->target(*walker, round.to);
    state.wardens.push_back({.walker = *walker, .round = round, .at = round.from});
}
```

## 复制即分叉

两个原生世界都由 `mlibs::simulation::Branching` 持有，所以整个状态是一个值：复制它就是一个分叉，Agent 可以在上面先玩下去；运行时的 `world.seek` 回到任意一步，逐位相同。

<!-- code: showcase/quay/src/sim/sim.cpp#branching -->
```cpp
state.bodies = mlibs::simulation::Branching<rigid3::World>(std::move(*bodies));
state.ground = mlibs::simulation::Branching<nav::World>(std::move(*ground));
```

## 人听到和看到的

人的视图（`quay::observe`）从不说出任何东西的位置。声音经 `xengine.audio.space` 的 `reach`（扬声器播放所依的同一条规则）穿过集装箱的钢板到达耳朵；太轻的听不到：

<!-- code: showcase/quay/src/headless/headless.cpp#hearing -->
```cpp
const space::Listener ears{.at = eye, .forward = {static_cast<float>(det::sin(yaw)), 0, static_cast<float>(-det::cos(yaw))}, .up = {0, 1, 0}};
bool first = true;
for (const Noise& noise : state.recent) {
    if (noise.by == 0) continue;
    const space::Reach reached = space::reach(ears, noise.at, state.statics->walls);
    const float level = noise.loudness + reached.level;
    if (level < heard_from) continue;
```

## 先由 CPU 参考画出，再上设备

`quay::Picture` 用 `xrender.mesh`（CPU 参考）从状态画出一帧：每台机器上像素相同。雨是 `xrender.particle`；辉光属于这一帧。`OnDevice` 在设备上画同一帧，并以这些像素为准。

<!-- code: showcase/quay/src/picture/picture.cpp#draw -->
```cpp
prepare(state);
// The reference's shadows are traced through each mesh's tree: the bent bodies' made again (a device's maps need none).
if (frame.shadows)
    for (std::size_t warden = 0; warden < bodies(); ++warden) grown_[body_mesh(warden)] = geom::bvh_of(made_[body_mesh(warden)].mesh);
targets_.resize(frame.width, frame.height);
mesh::Options options = options_;
options.shadows = frame.shadows;
pass_.draw(world_, seen_, view_, assets(frame.shadows), targets_, options);
rained_ = xrender::particle::draw(batches_, view_, targets_);
shown_ = targets_.colour;
bright_ = frame.glow ? glow_.show(targets_.light, frame.width, frame.height, bloom(frame.width), shown_).bright : 0;
return targets_;
```

## 运行

```sh
mcpp test -p omui.showcase.quay           # the harbour, the person's view, the agent playing as a person, the picture, the sound
mcpp test -p omui.showcase.quay.window    # played in a window; its recording the same bits with no window
mcpp test -p omui.showcase.quay.play      # on this machine's device, held to the CPU reference
```

## 出了问题怎么办

- **没有设备**：`OMUI_REQUIRE_GPU=1` 让缺少设备成为失败；在没有 GPU 的 Linux 上，`omui-devtools gpu` 装上 CI 所用的软件 Vulkan（lavapipe）。设备上的结果在[平台表](../platforms.md)里标为 `software`，不是 `real`。
- **回放不一致**：录像记下它的关卡并逐位回放；校验和不同说明状态的一步变了——`quay::State::checksum` 指出两次运行分开的第一步。
- **Web 上的帧**：页面 WebGPU 的帧时间只在真实 GPU 上按 16.7 ms 判定；在软件适配器上只打印、不判定（[平台表](../platforms.md)，`frame-budget-weak`）。
