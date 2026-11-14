module omui.bench;

import std;
import xrender.vector;
import omui.showcase.fighting;
import gongfu;
import gongfu.present2d;
import omui.showcase.widget_breakout.sim;
import xengine.world;
import omui.showcase.quay.picture;
import xrender.rhi.sdl;
import mlibs.rhi;
import mcpplibs.os;

namespace omui::bench {

namespace {

namespace p2 = gongfu::present2d;

// A busy match's inputs: directions held for a while, presses, now and then
// a motion (236, 623) ending in a button. The same sequence on every run.
class Script {
public:
    explicit Script(std::uint64_t seed) : s_(seed) {}
    gongfu::Pad next() {
        if (!queue_.empty()) {
            const gongfu::Pad p = queue_.front();
            queue_.erase(queue_.begin());
            return p;
        }
        const std::uint32_t v = draw();
        if (v % 23 == 0) {
            for (int d : {2, 3, 6}) queue_.push_back(gongfu::pad(d));
            queue_.push_back(gongfu::pad(6, gongfu::bit(gongfu::Button::lp)));
        } else if (v % 29 == 0) {
            for (int d : {6, 2, 3}) queue_.push_back(gongfu::pad(d));
            queue_.push_back(gongfu::pad(3, gongfu::bit(gongfu::Button::hp)));
        } else if (v % 5 == 0) {
            constexpr std::array<int, 9> dirs{6, 6, 6, 3, 5, 4, 2, 9, 6};  // mostly forward: they meet
            held_ = dirs[(v >> 5) % dirs.size()];
        }
        const std::uint16_t buttons = v % 4 == 1 ? static_cast<std::uint16_t>(1u << ((v >> 9) % 6)) : 0;
        return gongfu::pad(held_, buttons);
    }

private:
    std::uint32_t draw() {
        s_ ^= s_ << 13, s_ ^= s_ >> 7, s_ ^= s_ << 17;
        return static_cast<std::uint32_t>(s_ >> 11);
    }
    std::uint64_t            s_;
    int                      held_ = 5;
    std::vector<gongfu::Pad> queue_;
};

// A match between the roster's first fighter and itself, played by two scripts;
// a round that ends starts again (outside the timing).
struct Fight {
    gongfu::Match match;
    Script        a{11}, b{29};
    Fight() : match(fighting::roster().system, fighting::roster().fighters.at(0), fighting::roster().fighters.at(0)) {}
    void tick() { match.tick(a.next(), b.next()); }
    void again() { match = gongfu::Match(fighting::roster().system, fighting::roster().fighters.at(0), fighting::roster().fighters.at(0)); }
};

namespace bo = omui::breakout;

// The breakout's simulation over a full wall: twelve columns, eight rows of
// bricks (the top two switches), a player under the lowest falling ball who
// launches at once — the same every run.
bo::Level breakout_wall() {
    bo::Level l;
    std::uint32_t id = 0;
    for (std::uint32_t row = 0; row < 8; ++row)
        for (int col = 0; col < 12; ++col)
            l.bricks.push_back({xengine::dynamics::Box{70.0 + col * 96, 80.0 + row * 36, 88, 28}, row < 2 ? bo::Kind::toggle : bo::Kind::button, row < 2 ? 2 : 1, id++, row});
    l.rules.multi_balls = 4;  // capsules make it busy
    l.rules.drop_one_in = 3;
    return l;
}

bo::Input breakout_player(const bo::State& s) {
    bo::Input in;
    double x = bo::field_w / 2, lowest = -1;
    s.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) {
        if (b.on == 0 && b.at.y > lowest) lowest = b.at.y, x = b.at.x;
    });
    in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(x, 0.0, bo::field_w) / bo::field_w * 32767));
    if (s.frame % 30 == 0) in.player.pressed = in.player.held = 1u << bo::launch_button;
    return in;
}

}  // namespace

namespace chunked {
struct Position {
    float x = 0, y = 0, z = 0;
    static constexpr bool no_padding = true;
};
struct Velocity {
    float x = 0, y = 0, z = 0;
    static constexpr bool no_padding = true;
};
struct Health {
    std::uint32_t points = 0;
};
using World = xengine::Chunked<Position, Velocity, Health>;
// 100,000 entities, all with a place, two in three moving, one in five with health.
World made() {
    World world;
    for (std::uint32_t index = 0; index < 100000; ++index) {
        const auto id = world.create();
        world.add(id, Position{static_cast<float>(index % 1000), 0, static_cast<float>(index / 1000)});
        if (index % 3) world.add(id, Velocity{1, 0, 0.5f});
        if (index % 5 == 0) world.add(id, Health{100});
    }
    return world;
}
}  // namespace chunked

void add_game_benchmarks() {
    add("game/quay-frame-1080p-gpu", "a frame of Quay, the FPS slice, at 1920x1080 on this machine's device as a game plays it: the step (Jolt's world, Detour's crowd, what the wardens hear and see), the guide's choice, the picture prepared (four wardens posed by their skeletons and bent, the lights, the view, what it sees), and on the device the bodies bent, the moon's map, eight lights' six views each, the light, the glow and the curve — submitted and waited for (D-187: p99 within 8.3 ms on a desktop GPU)",
        [](State& s) {
            namespace rhi = mlibs::rhi;
            constexpr std::uint32_t width = 1920, height = 1080;
            xrender::rhi::sdl::Options options;
            options.formats = xrender::mesh::gpu::Pass::shader_formats();
            if (auto library = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *library;
            if (auto video = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *video;
            auto device = xrender::rhi::sdl::create(options);
            if (!device) {
                s.skip(std::format("no GPU device ({})", device.error().message));
                return;
            }
            rhi::Device& on = **device;
            s.note("device", std::format("{} ({}{})", on.caps().device, rhi::name(on.caps().backend), on.caps().software ? ", software" : ""));
            auto picture = quay::picture::Picture::make(quay::level::harbour());
            if (!picture) {
                s.fail(picture.error());
                return;
            }
            auto drawn = quay::picture::OnDevice::make(on, *picture, width, height);
            if (!drawn) {
                s.fail(std::format("Quay's picture on the device: {}", drawn.error().message));
                return;
            }
            // A second in, untimed: what is measured has the first warden in view and the first shots.
            quay::State state = quay::start(quay::level::harbour());
            quay::Guide guide;
            while (state.frame < 60) quay::step(state, guide(state));
            rhi::CommandList list;  // kept: a list cleared keeps its room
            std::uint64_t games = 0;
            const auto frame = [&] {
                if (state.outcome != quay::Outcome::playing) {
                    s.pause();
                    state = quay::start(quay::level::harbour()), guide = {}, ++games;
                    s.resume();
                }
                const auto begun = std::chrono::steady_clock::now();
                quay::step(state, guide(state));
                picture->prepare(state);
                list.clear();
                if (!drawn->record(list, *picture)) {
                    s.fail("the frame could not be recorded");
                    return;
                }
                const auto submitted = on.submit(list);
                s.counter("encode_ms", std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begun).count());
                const auto waited = on.wait_idle();
                if (!submitted || !waited) {
                    s.fail(std::format("the frame failed: {}", !submitted ? submitted.error().message : waited.error().message));
                    return;
                }
                s.counter("software", on.caps().software ? 1 : 0);
            };
            frame();
            s.items(width * height);
            while (s.next()) frame();
            const auto& last = drawn->last();
            s.note("scope", std::format("one thread; {} draws, {} lights, {} views of lamps, {} casters in the moon's map; {} games played to the end and begun again (not timed); the same frames the device suite holds to the reference at 480x270",
                                        last.draws, last.lamps, last.lamp_views, last.casters, games));
        });
    add("world/chunked-branch-100k", "a branch of a world of 100,000 entities (xengine.world's Chunked, 1,565 chunks): the copy, one entity written in it, the two compared — what a snapshot, a rewind point or an agent's what-if costs (D-169)",
        [](State& s) {
            const chunked::World world = chunked::made();
            std::uint64_t which = 0;
            while (s.next()) {
                chunked::World branch = world;
                which = which % 100000 + 1;
                branch.edit<chunked::Position>(which)->y += 1;
                if (branch.changed_since(world).chunks_own != 1) s.note("wrong", "a write made more than one chunk its own");
            }
            s.note("scope", "one thread; the copy shares every chunk and directory page, the write copies one chunk; World<N>'s block form copies every byte instead");
        });
    add("world/chunked-step-100k", "a step over a branched world of 100,000 entities: each moving entity's place by its speed (66,667 of them), every chunk they are in copied once since the branch shares it",
        [](State& s) {
            const chunked::World world = chunked::made();
            while (s.next()) {
                s.pause();
                chunked::World branch = world;
                s.resume();
                branch.each<chunked::Position, const chunked::Velocity>([](chunked::World::Id, chunked::Position& at, const chunked::Velocity& by) {
                    at.x += by.x * (1.0f / 60), at.z += by.z * (1.0f / 60);
                });
            }
            s.items(66667);
            s.note("scope", "one thread; the first write to each shared chunk copies it (the worst case of a branch: all of them), the copy itself not timed");
        });
    add("breakout/step", "one step of the widget breakout's simulation over a full wall: paddles, swept balls (several), bricks, capsules (xengine S4 design §5)", [](State& s) {
        bo::State st = bo::start(breakout_wall());
        for (int i = 0; i < 600; ++i) bo::step(st, breakout_player(st));  // under way: balls in the wall
        std::int64_t balls = 0, steps = 0;
        while (s.next()) {
            if (st.over) {
                s.pause();
                st = bo::start(breakout_wall());
                s.resume();
            }
            bo::step(st, breakout_player(st));
            st.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball&) { ++balls; });
            ++steps;
        }
        s.counter("balls_per_step", static_cast<double>(balls) / static_cast<double>(std::max<std::int64_t>(steps, 1)));
    });
    add("breakout/seek", "a rewind of the widget breakout: the timeline seeks 0.5–30 s back (the snapshot before, then the steps after it again)", [](State& s) {
        mlibs::simulation::Timeline<bo::Game> t(bo::start(breakout_wall()), 30 * bo::steps_per_second, 60);
        for (int i = 0; i < 40 * bo::steps_per_second; ++i) t.step(breakout_player(t.state()));
        std::uint64_t r = 7;
        while (s.next()) {
            r ^= r << 13, r ^= r >> 7, r ^= r << 17;
            const std::int64_t back = 60 + static_cast<std::int64_t>(r % static_cast<std::uint64_t>(t.latest() - t.earliest() - 60));
            (void)t.seek(t.latest() - back);
        }
    });
    add("gongfu/frame", "one frame of the fighting game's simulation: two fighters' input, moves, boxes, hits (D-85 §8.6)", [](State& s) {
        Fight f;
        for (int i = 0; i < 300; ++i) f.tick();  // past the opening: the fighters close in
        std::int64_t hits = 0, frames = 0;
        while (s.next()) {
            if (f.match.state().over) {
                s.pause();
                f.again();
                s.resume();
            }
            f.tick();
            hits += static_cast<std::int64_t>(f.match.events().size());
            ++frames;
        }
        s.counter("events_per_100_frames", 100.0 * static_cast<double>(hits) / static_cast<double>(std::max<std::int64_t>(frames, 1)));
    });
    add("gongfu/rollback-8", "a rollback of eight frames: the state eight frames back restored, the frames simulated again, each saved (D-85 §8.6)",
        [](State& s) {
            Fight f;
            for (int i = 0; i < 300; ++i) f.tick();
            constexpr std::size_t depth = 8;
            std::array<gongfu::State, depth + 1> saved{};
            std::array<std::pair<gongfu::Pad, gongfu::Pad>, depth> inputs{};
            s.items(depth);
            while (s.next()) {
                s.pause();
                if (f.match.state().over) f.again();
                saved[0] = f.match.state();
                for (std::size_t i = 0; i < depth; ++i) {
                    inputs[i] = {f.a.next(), f.b.next()};
                    f.match.tick(inputs[i].first, inputs[i].second);
                }
                s.resume();
                f.match.restore(saved[0]);
                for (std::size_t i = 0; i < depth; ++i) {
                    f.match.tick(inputs[i].first, inputs[i].second);
                    saved[i + 1] = f.match.state();
                }
            }
        });
    add("gongfu/present2d", "one frame's picture of the fighting game built as a display list: the stage, two skeletons, effects (no raster)", [](State& s) {
        Fight f;
        p2::Effects effects;
        const p2::Scene scene;
        const auto& looks = fighting::roster().looks;
        xrender::DisplayList dl;
        dl.reserve(512);  // as a game's frame list does: its frames vary in size
        p2::Painter painter;
        // Warm: the painter's paths and the list's arena reach the size of a busy frame.
        for (int i = 0; i < 1200; ++i) {
            f.tick();
            effects.record(f.match);
            dl.clear();
            painter.draw(f.match, looks.at(0), looks.at(1), effects, scene, dl);
        }
        while (s.next()) {
            s.pause();
            if (f.match.state().over) f.again();
            f.tick();
            effects.record(f.match);
            dl.clear();
            s.resume();
            painter.draw(f.match, looks.at(0), looks.at(1), effects, scene, dl);
        }
        s.counter("commands", static_cast<double>(dl.size()));
    });
}

}  // namespace omui::bench
