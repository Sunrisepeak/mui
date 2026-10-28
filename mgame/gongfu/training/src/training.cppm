// gongfu.training — what training mode shows, measured (D-85 §8.5).
//
//     gongfu::training::Readout readout;
//     match.tick(p1, p2);
//     readout.record(match, p1, p2);           // after each frame
//     readout.advantage();                     // +3: the attacker acts three frames first
//     readout.meter(0);                        // the last frames' phases, oldest first
//
// Everything is read from the match after each frame — the state, its events,
// whether each fighter has recovered — so it holds for any fighter's data,
// and it allocates nothing (fixed rings). After a rollback a Readout is
// cleared, or recorded again from the frame restored; it is a person's view,
// not part of the simulation.
export module gongfu.training;

import std;
export import gongfu;

export namespace gongfu::training {

// What a fighter's frame is, for the meter.
enum class Phase : std::uint8_t { free, startup, active, recovery, hitstun, blockstun, down, thrown };

[[nodiscard]] Phase phase(const Match& match, std::int32_t fighter) noexcept;

// A screen-absolute numpad direction of a pad (5: none; left and right together count as neither).
[[nodiscard]] int direction(Pad pad) noexcept;

class Readout {
public:
    static constexpr std::size_t meter_frames = 80;
    static constexpr std::size_t history = 16;

    // A pad held for some frames: one line of the input history.
    struct Input {
        Pad          pad;
        std::int32_t frames = 0;
    };
    struct Combo {
        std::int32_t attacker = -1;  // −1: none yet
        std::int32_t hits = 0;
        std::int32_t damage = 0;
        bool         going = false;   // the defender is still in it
    };

    void record(const Match& match, Pad left, Pad right) noexcept;
    void clear() noexcept;

    // The pads, newest last (a press is a new line; a held pad counts its frames).
    [[nodiscard]] std::span<const Input> inputs(std::int32_t fighter) const noexcept;
    // The last frames' phases, oldest first; frames of hitstop and of a
    // super's flash are not counted (they are not frames of the moves).
    [[nodiscard]] std::span<const Phase> meter(std::int32_t fighter) const noexcept;
    // The frames between the two sides' recovery after the last hit or block,
    // from the attacker's side (positive: it acts first); none until both recovered.
    [[nodiscard]] std::optional<std::int32_t> advantage() const noexcept { return advantage_; }
    [[nodiscard]] std::int32_t advantage_attacker() const noexcept { return adv_attacker_; }
    // The combo going on, or the last one.
    [[nodiscard]] const Combo& combo() const noexcept { return combo_; }
    // The startup of the last move each fighter began, as played: its first active frame.
    [[nodiscard]] std::optional<std::int32_t> startup(std::int32_t fighter) const noexcept;

private:
    struct Side {
        std::array<Input, history>       inputs{};
        std::size_t                      input_count = 0;
        std::array<Phase, meter_frames>  meter{};
        std::size_t                      meter_count = 0;
        std::int32_t                     move = -1;  // the move it is in
        std::optional<std::int32_t>      startup;
    };
    std::array<Side, 2>          sides_{};
    std::optional<std::int32_t>  advantage_;
    std::int32_t                 adv_attacker_ = -1;
    std::int32_t                 pending_attacker_ = -1;  // a contact waiting for both sides to recover
    std::int32_t                 waited_ = 0;             // frames since it
    std::array<std::int32_t, 2>  free_at_{-1, -1};        // the frame (counted from the contact) each recovered
    Combo                        combo_;
    bool                         frozen_next_ = false;    // the next frame stands still (hitstop, a flash)
};

// A player's pads, kept and played back looped: training's dummy.
class Recorder {
public:
    static constexpr std::size_t capacity = 60 * 30;  // thirty seconds

    void start_recording() noexcept;
    void start_playback() noexcept;
    void stop() noexcept;
    // While recording, keeps the pad and answers it; while playing, answers the
    // recording's next pad (from the start again after its last); else the pad.
    [[nodiscard]] Pad step(Pad given) noexcept;
    [[nodiscard]] bool recording() const noexcept { return mode_ == Mode::recording; }
    [[nodiscard]] bool playing() const noexcept { return mode_ == Mode::playing; }
    [[nodiscard]] std::size_t length() const noexcept { return length_; }

private:
    enum class Mode : std::uint8_t { idle, recording, playing };
    std::array<Pad, capacity> pads_{};
    std::size_t               length_ = 0, at_ = 0;
    Mode                      mode_ = Mode::idle;
};

}  // namespace gongfu::training
