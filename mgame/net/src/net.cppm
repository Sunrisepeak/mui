// mgame.net — rollback netcode for two peers (D-85), independent of the
// transport.
//
// Each peer runs the whole game and sends only its own inputs. A local input
// plays `delay` frames after it is given (an input delay hides some of the
// latency); the peer's input for a frame not yet received is predicted —
// its last known one. When the real input arrives and differs from what was
// predicted, the session restores the state saved at that frame and plays
// forward again with the input corrected: the rollback. A peer never runs
// more than `max_rollback` frames past the last frame it has the other's
// input for; it waits instead. Every `checksum_every` confirmed frames the
// peers exchange the state's checksum, so a desync is named with its frame.
//
// A message carries every local input the peer has not acknowledged (a lost
// one is sent again with the next), the acknowledgement of the peer's, a
// checksum, and the sender's frame (how far ahead one peer runs: what time
// sync evens out). encode() and decode() make it bytes for any transport.
//
//     mgame::net::Session session(game, local_player, {.delay = 2, .max_rollback = 8});
//     transport.send(mgame::net::encode(session.local(input)));
//     for (auto& bytes : transport.received()) if (auto m = mgame::net::decode(bytes)) session.receive(*m);
//     session.advance();   // advanced, or waiting for the peer
export module mgame.net;

import std;

export namespace mgame::net {

// What a game is to a session: one frame from both players' inputs, its state
// saved and restored by a copy, and a checksum of a saved state.
template <class G>
concept Game = requires(G& g, const G& cg, const typename G::Input& in, const typename G::State& s) {
    requires std::is_trivially_copyable_v<typename G::Input>;
    // Sent as its own bytes: none of them padding, so equal inputs are equal bytes.
    requires std::has_unique_object_representations_v<typename G::Input>;
    requires std::equality_comparable<typename G::Input>;
    requires std::default_initializable<typename G::Input>;
    g.advance(in, in);  // player 0's input, player 1's
    { cg.save() } -> std::convertible_to<typename G::State>;
    g.load(s);
    { cg.checksum_of(s) } -> std::convertible_to<std::uint64_t>;
};

// Out of range, a session takes the nearest it accepts (valid()).
struct Options {
    std::int32_t delay = 2;            // frames between giving a local input and the frame it plays on (0–60)
    std::int32_t max_rollback = 8;     // the most frames a correction reaches back (0–120)
    std::int32_t checksum_every = 30;  // frames between checksum exchanges (at least 1)

    [[nodiscard]] constexpr Options valid() const noexcept {
        return {std::clamp(delay, 0, 60), std::clamp(max_rollback, 0, 120), std::max(checksum_every, 1)};
    }
};

// An input is its own bytes on the wire, and every target openmui builds for
// is little-endian; a big-endian one would need the input encoded field by field.
static_assert(std::endian::native == std::endian::little, "mgame.net sends inputs as their bytes: a little-endian target");

struct Message {
    std::int32_t           first = 0;            // the frame of the first input
    std::int32_t           count = 0;
    std::vector<std::byte> inputs;               // count inputs, each the Input's bytes
    std::int32_t           ack = -1;             // the last frame of the receiver's inputs the sender has
    std::int32_t           frame = 0;            // the sender's next frame
    std::int32_t           checksum_frame = -1;  // a confirmed frame, and the checksum of the state at its start
    std::uint64_t          checksum = 0;
};

// Little-endian, field by field: the same bytes on every machine.
[[nodiscard]] std::vector<std::byte> encode(const Message& m);
[[nodiscard]] std::optional<Message> decode(std::span<const std::byte> bytes);

enum class Step : std::uint8_t { advanced, waiting };

template <Game G>
class Session {
public:
    using Input = typename G::Input;
    using State = typename G::State;

    Session(G& game, std::int32_t local, Options options = {})
        : game_(&game), local_(local == 1 ? 1 : 0), options_(options.valid()),
          ring_(static_cast<std::size_t>(options_.max_rollback + 2 * options_.delay + 64)) {
        for (Slot& s : ring_) s = {};
        // The frames before the first local input plays: neutral, for both, known to both.
        for (std::int32_t f = 0; f < options_.delay; ++f) {
            slot(f).mine = Input{};
            slot(f).mine_set = true;
            slot(f).theirs = Input{};
            slot(f).theirs_set = true;
        }
        queued_ = options_.delay;
        theirs_confirmed_ = options_.delay - 1;
    }

    // The local input for the frame `delay` frames ahead; the message to send.
    [[nodiscard]] Message local(const Input& in) {
        Slot& s = slot(queued_);
        s.frame = queued_;
        s.mine = in;
        s.mine_set = true;
        ++queued_;
        return message();
    }

    // The message to send now (inputs not yet acknowledged, the ack, a checksum).
    [[nodiscard]] Message message() const {
        Message m;
        m.first = std::max(peer_ack_ + 1, 0);
        m.count = std::max(0, queued_ - m.first);
        m.inputs.resize(static_cast<std::size_t>(m.count) * sizeof(Input));
        for (std::int32_t k = 0; k < m.count; ++k) {
            const Input& in = slot_const(m.first + k).mine;
            std::memcpy(m.inputs.data() + static_cast<std::size_t>(k) * sizeof(Input), &in, sizeof(Input));
        }
        m.ack = theirs_confirmed_;
        m.frame = frame_;
        m.checksum_frame = report_frame_;
        m.checksum = report_;
        return m;
    }

    void receive(const Message& m) {
        peer_ack_ = std::max(peer_ack_, std::min(m.ack, queued_ - 1));
        peer_frame_ = std::max(peer_frame_, m.frame);
        if (m.inputs.size() != static_cast<std::size_t>(m.count) * sizeof(Input)) return;
        for (std::int32_t k = 0; k < m.count; ++k) {
            const std::int32_t f = m.first + k;
            if (f <= theirs_confirmed_ || f < frame_ - static_cast<std::int32_t>(ring_.size()) + 1) continue;
            if (f != theirs_confirmed_ + 1) break;  // in order: a gap waits for the resend
            Slot& s = slot(f);
            if (s.frame != f) s = Slot{.frame = f};
            std::memcpy(&s.theirs, m.inputs.data() + static_cast<std::size_t>(k) * sizeof(Input), sizeof(Input));
            s.theirs_set = true;
            theirs_confirmed_ = f;
            // A frame already played on a prediction that was wrong: roll back to the first.
            if (f < frame_ && s.played && !(s.used == s.theirs)) rollback_to_ = std::min(rollback_to_, f);
        }
        if (m.checksum_frame >= 0 && m.checksum_frame <= confirmed()) {
            if (const auto mine = checksum_at(m.checksum_frame); mine && *mine != m.checksum && !desync_) desync_ = m.checksum_frame;
        }
    }

    // Applies what the peer's inputs corrected: back to the first frame
    // played on a wrong prediction, and forward again to where it was.
    void correct() {
        if (rollback_to_ < frame_) {
            const std::int32_t from = rollback_to_;
            game_->load(slot(from).saved);
            for (std::int32_t f = from; f < frame_; ++f) play(f);
            ++rollbacks_;
            rolled_frames_ += frame_ - from;
        }
        rollback_to_ = std::numeric_limits<std::int32_t>::max();
    }

    // One frame, if the peer is not too far behind; corrections first.
    Step advance() {
        correct();
        if (frame_ >= queued_) return Step::waiting;                               // no local input yet
        if (frame_ - theirs_confirmed_ > options_.max_rollback) return Step::waiting;  // too far past the peer
        play(frame_);
        ++frame_;
        report();
        return Step::advanced;
    }

    [[nodiscard]] std::int32_t frame() const noexcept { return frame_; }
    // The last frame both players' inputs are known for.
    [[nodiscard]] std::int32_t confirmed() const noexcept { return std::min(theirs_confirmed_, frame_ - 1); }
    [[nodiscard]] std::int32_t rollbacks() const noexcept { return rollbacks_; }
    [[nodiscard]] std::int64_t rolled_frames() const noexcept { return rolled_frames_; }
    // How many frames this peer runs ahead of the other (time sync slows the one ahead).
    [[nodiscard]] std::int32_t ahead() const noexcept { return frame_ - peer_frame_; }
    // The first frame whose checksum the peers disagree on.
    [[nodiscard]] std::optional<std::int32_t> desync() const noexcept { return desync_; }

private:
    struct Slot {
        std::int32_t frame = -1;
        Input        mine{}, theirs{}, used{};  // used: the peer's input this frame was played with
        bool         mine_set = false, theirs_set = false, played = false;
        State        saved{};                   // the state at the frame's start
    };

    Slot& slot(std::int32_t f) {
        Slot& s = ring_[static_cast<std::size_t>(f) % ring_.size()];
        if (s.frame != f) s = Slot{.frame = f};  // a frame the ring no longer needed
        return s;
    }
    [[nodiscard]] const Slot& slot_const(std::int32_t f) const { return ring_[static_cast<std::size_t>(f) % ring_.size()]; }

    // Plays frame f from the state the game holds (that frame's start).
    void play(std::int32_t f) {
        Slot& s = slot(f);
        s.saved = game_->save();
        const Input theirs = s.theirs_set ? s.theirs : predicted(f);
        s.used = theirs;
        s.played = true;
        if (local_ == 0) game_->advance(s.mine, theirs);
        else game_->advance(theirs, s.mine);
    }

    // The peer's input for a frame it has not sent yet: its last known.
    [[nodiscard]] Input predicted(std::int32_t f) const {
        const std::int32_t last = std::min(theirs_confirmed_, f - 1);
        return last >= 0 ? slot_const(last).theirs : Input{};
    }

    // The checksum of the state at frame f's start, while it is held.
    [[nodiscard]] std::optional<std::uint64_t> checksum_at(std::int32_t f) const {
        const Slot& s = slot_const(f);
        if (s.frame != f || !s.played) return std::nullopt;
        return game_->checksum_of(s.saved);
    }

    // The newest confirmed frame on the checksum period, for the next message.
    void report() {
        const std::int32_t c = confirmed();
        const std::int32_t f = c - c % options_.checksum_every;
        if (f >= 0 && f > report_frame_ && f >= frame_ - static_cast<std::int32_t>(ring_.size()) + 1) {
            if (const auto sum = checksum_at(f)) report_frame_ = f, report_ = *sum;
        }
    }

    G*                 game_;
    std::int32_t       local_;
    Options            options_;
    std::vector<Slot>  ring_;
    std::int32_t       frame_ = 0;              // the next frame to play
    std::int32_t       queued_ = 0;             // the next frame a local input is given for
    std::int32_t       theirs_confirmed_ = -1;  // the last frame of the peer's inputs received, in order
    std::int32_t       peer_ack_ = -1;          // the last frame of my inputs the peer has
    std::int32_t       peer_frame_ = 0;
    std::int32_t       rollback_to_ = std::numeric_limits<std::int32_t>::max();
    std::int32_t       rollbacks_ = 0;
    std::int64_t       rolled_frames_ = 0;
    std::int32_t       report_frame_ = -1;
    std::uint64_t      report_ = 0;
    std::optional<std::int32_t> desync_;
};

}  // namespace mgame::net
