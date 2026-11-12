// hanim.agent — the stage as a subject of the agent kernel (D-173, D-174):
// `animation`, whose operations are hanim.control's catalog
// (hanim.ops.omui.ts) — prepare and play a program, list and inspect what
// plays, seek, pause, reverse, change the rate, cancel, release.
//
//     hanim::agent::Stage stage(engine);
//     stage.offer("intro", anim::to(card, PropertyTargets{.position = Vec2{200, 50}}).duration(seconds(1)));
//     mlibs::agent::SubjectSession session({&stage});
//     // → {"op":"animation.play","program":"intro"}        ← {"playback":{"slot":0,"generation":1}}
//     // → {"op":"animation.seek-progress","playback":{"slot":0,"generation":1},"progress":0.5}
//
// A program is a composition its owner builds (C++ builders, or a document
// hanim.studio reads); the stage offers them by name, and a request names
// one. Pure animation is a function of time: a playback sought to a
// position is that position's pose, whoever asks and whenever — the same
// requests, the same bits.
//
// What it cannot do yet it says: a pose previewed without touching the
// playback (a branch), and why a value is what it is — which layers and
// blends it came from (the plan's §3.4: missing).
export module hanim.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
export import hanim.engine;
export import hanim.control;

export namespace hanim::agent {

class Stage : public mlibs::agent::Subject {
public:
    // The engine outlives the stage; whoever owns it advances its clock.
    explicit Stage(Engine& engine) : engine_(engine) {}
    // A program requests may prepare and play, by name (offered again: replaced).
    void offer(std::string name, Program program);

    [[nodiscard]] std::string_view name() const noexcept override { return "animation"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "hanim.animation@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true, .time = true}; }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return hanim::control::operations(); }
    [[nodiscard]] std::string_view listing() const noexcept override { return hanim::control::listing; }
    // The engine's time and every playback's place in its program: the same stage, the same hash.
    [[nodiscard]] std::uint64_t state_hash() const override;
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    [[nodiscard]] std::vector<PlaybackInfo> playing() const;

    Engine&                                       engine_;
    std::vector<std::pair<std::string, Program>>  offered_;
};

}  // namespace hanim::agent
