module openmui.bench;

import std;
import openmui.render.vector;
import openmui.showcase.fighting;
import gongfu;
import gongfu.present2d;

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

}  // namespace

void add_game_benchmarks() {
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
        render::DisplayList dl;
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
