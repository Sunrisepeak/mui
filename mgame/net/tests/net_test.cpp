// Rollback netcode over a simulated network: two peers playing gongfu's
// match send each other only their inputs, through a link with latency,
// jitter and loss; each predicts the other, rolls back when wrong, waits when
// too far ahead. Whatever the link, both end on the bytes of an offline run
// of the same inputs, every exchanged checksum agrees, and a peer whose game
// differs is named with the frame it parted at.
import std;
import mgame.net;
import gongfu;
import openmui.testing;

using namespace omui::testing;

namespace {

namespace net = mgame::net;

// gongfu's match as a session's game.
struct Fight {
    using Input = gongfu::Pad;
    using State = gongfu::State;
    gongfu::Match match;
    std::int32_t  tamper_at = -1;  // a frame on which this peer's game quietly differs
    void advance(const Input& a, const Input& b) {
        match.tick(a, b);
        if (match.state().frame == tamper_at) {
            auto st = match.state();
            st.f[0].health -= 1;
            match.restore(st);
        }
    }
    [[nodiscard]] State save() const { return match.state(); }
    void load(const State& s) { match.restore(s); }
    [[nodiscard]] std::uint64_t checksum_of(const State& s) const { return gongfu::checksum(s); }
};
static_assert(net::Game<Fight>);

gongfu::FighterDef fighter() {
    using namespace gongfu;
    FighterDef f;
    f.name = "Net";
    f.health = 1 << 24;
    f.walk_forward = 4_fx;
    f.walk_back = 3_fx;
    f.jump_up = 20_fx;
    f.jump_forward = 5_fx;
    f.gravity = 1_fx;
    f.push = {-30_fx, 0_fx, 60_fx, 160_fx};
    f.hurt_standing = {-35_fx, 0_fx, 70_fx, 180_fx};
    f.hurt_crouching = {-35_fx, 0_fx, 70_fx, 110_fx};
    f.hurt_air = {-30_fx, 40_fx, 60_fx, 130_fx};
    MoveDef jab{.name = "5LP", .command = {.buttons = bit(Button::lp)}, .total = 12};
    jab.hits.push_back({.active = {4, 5}, .boxes = {{20_fx, 110_fx, 70_fx, 30_fx}}, .damage = 300, .hitstun = 15, .blockstun = 11, .hitstop = 6,
                        .pushback = 2_fx});
    f.moves.push_back(jab);
    MoveDef sweep{.name = "2HK", .command = {.direction = 2, .buttons = bit(Button::hk)}, .stance = Stance::crouching, .total = 30, .priority = 1};
    sweep.hits.push_back({.active = {8, 10}, .boxes = {{10_fx, 0_fx, 110_fx, 30_fx}}, .damage = 900, .hitstun = 20, .blockstun = 14, .hitstop = 9,
                          .pushback = 3_fx, .level = Level::low, .knockdown = true});
    f.moves.push_back(sweep);
    MoveDef grab{.name = "throw", .command = {.buttons = static_cast<std::uint16_t>(bit(Button::lp) | bit(Button::lk)), .together = true},
                 .total = 30, .priority = 20};
    grab.throw_ = ThrowDef{.active = {3, 4}, .range = {0_fx, 0_fx, 80_fx, 150_fx}, .damage = 1200};
    f.moves.push_back(grab);
    return f;
}

const gongfu::Fighter& compiled() {
    static const gongfu::Fighter f = *gongfu::compile(fighter());
    return f;
}

gongfu::SystemDef rules() {
    gongfu::SystemDef s;
    s.round_frames = 1 << 20;
    return s;
}

struct Rng {
    std::uint64_t s;
    std::uint32_t next() {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 11);
    }
};

// Inputs a person holds for a while, as people do.
std::vector<gongfu::Pad> script(std::uint64_t seed, int frames) {
    Rng r{seed};
    std::vector<gongfu::Pad> out;
    gongfu::Pad held{};
    for (int f = 0; f < frames; ++f) {
        if (r.next() % 6 == 0) {
            const int dirs[] = {5, 4, 6, 2, 3, 1, 8, 5, 5};
            const std::uint32_t v = r.next();
            std::uint16_t buttons = 0;
            if (v % 3 == 0) buttons = static_cast<std::uint16_t>(1u << (v % 6));
            held = gongfu::pad(dirs[(v >> 4) % std::size(dirs)], buttons);
        } else {
            held.buttons = 0;  // a press is one frame; a direction is held
        }
        out.push_back(held);
    }
    return out;
}

// A one-way link: each message delayed `latency` ± `jitter` ticks, some lost,
// none reordered past one another's delivery time.
struct Link {
    int    latency = 0, jitter = 0;
    double loss = 0;
    Rng    rng{7};
    std::multimap<int, std::vector<std::byte>> in_flight;
    void send(int now, std::vector<std::byte> bytes) {
        if (loss > 0 && (rng.next() % 1000) < static_cast<std::uint32_t>(loss * 1000)) return;
        const int j = jitter ? static_cast<int>(rng.next() % static_cast<std::uint32_t>(2 * jitter + 1)) - jitter : 0;
        in_flight.emplace(now + std::max(0, latency + j), std::move(bytes));
    }
    std::vector<std::vector<std::byte>> arrive(int now) {
        std::vector<std::vector<std::byte>> out;
        while (!in_flight.empty() && in_flight.begin()->first <= now) {
            out.push_back(std::move(in_flight.begin()->second));
            in_flight.erase(in_flight.begin());
        }
        return out;
    }
};

struct Outcome {
    std::uint64_t a = 0, b = 0, offline = 0;
    int           rollbacks = 0, waited = 0;
    std::optional<std::int32_t> desync;
    std::int32_t  frames = 0;
    std::int32_t  checksums = 0;
};

// Plays `frames` frames on two peers over the link; returns their states'
// checksums at the end and an offline run's.
Outcome play(int frames, Link ab, Link ba, net::Options options, bool tamper = false) {
    const auto& f = compiled();
    const auto p0 = script(11, frames + options.delay + 1), p1 = script(29, frames + options.delay + 1);
    Fight ga{gongfu::Match(rules(), f, f), tamper ? 200 : -1}, gb{gongfu::Match(rules(), f, f)};
    net::Session<Fight> a(ga, 0, options), b(gb, 1, options);
    Outcome out;
    int now = 0, given_a = 0, given_b = 0;
    for (; now < frames * 20 && (a.frame() < frames || b.frame() < frames || a.confirmed() < frames - 1 || b.confirmed() < frames - 1); ++now) {
        // Each peer gives its input for the frame `delay` ahead once its game
        // has played the frame before (a frame of its own clock), else just
        // sends what the other has not acknowledged.
        auto give = [&](net::Session<Fight>& peer, int& given, const std::vector<gongfu::Pad>& inputs, Link& link) {
            const int q = options.delay + given;
            if (given <= peer.frame() && q < static_cast<int>(inputs.size())) {
                link.send(now, net::encode(peer.local(inputs[static_cast<std::size_t>(q)])));
                ++given;
            } else {
                link.send(now, net::encode(peer.message()));
            }
        };
        give(a, given_a, p0, ab);
        give(b, given_b, p1, ba);
        for (auto& m : ab.arrive(now))
            if (auto d = net::decode(m)) b.receive(*d);
        for (auto& m : ba.arrive(now))
            if (auto d = net::decode(m)) a.receive(*d);
        if (a.frame() < frames && a.advance() == net::Step::waiting) ++out.waited;
        if (b.frame() < frames && b.advance() == net::Step::waiting) ++out.waited;
        a.correct();  // past the last frame, a late input still corrects
        b.correct();
    }
    out.frames = std::min(a.frame(), b.frame());
    out.a = ga.match.checksum();
    out.b = gb.match.checksum();
    out.rollbacks = a.rollbacks() + b.rollbacks();
    out.desync = a.desync() ? a.desync() : b.desync();
    // Offline: the inputs each frame actually played with.
    gongfu::Match offline(rules(), f, f);
    for (int fr = 0; fr < frames; ++fr) {
        const gongfu::Pad i0 = fr < options.delay ? gongfu::Pad{} : p0[static_cast<std::size_t>(fr)];
        const gongfu::Pad i1 = fr < options.delay ? gongfu::Pad{} : p1[static_cast<std::size_t>(fr)];
        offline.tick(i0, i1);
    }
    out.offline = offline.checksum();
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"mgame.net"};

    s.test("messages: bytes that read back, and refuse what is not one", [] {
        net::Message m{.first = 12, .count = 2, .inputs = std::vector<std::byte>(8, std::byte{3}), .ack = 9, .frame = 15, .checksum_frame = 0,
                       .checksum = 0x0123456789abcdefULL};
        const auto bytes = net::encode(m);
        auto back = net::decode(bytes);
        check(back && back->first == 12 && back->count == 2 && back->inputs == m.inputs && back->ack == 9 && back->frame == 15 &&
              back->checksum == m.checksum);
        auto cut = bytes;
        cut.pop_back();
        check(!net::decode(cut), "a short message");
        auto wrong = bytes;
        wrong[0] = std::byte{0};
        check(!net::decode(wrong), "not a message of this format");
    });

    s.test("a perfect link: no rollback, both peers and the offline run alike", [] {
        const auto o = play(600, Link{}, Link{}, {.delay = 2});
        check(o.frames == 600 && o.a == o.b && o.a == o.offline, std::format("{} frames; {:016x} {:016x} offline {:016x}", o.frames, o.a, o.b, o.offline));
        check_eq(o.rollbacks, 0);
    });

    s.test("latency within the input delay: still no rollback", [] {
        const auto o = play(600, Link{.latency = 2}, Link{.latency = 2}, {.delay = 3});
        check(o.a == o.b && o.a == o.offline);
        check_eq(o.rollbacks, 0);
    });

    s.test("latency past the delay, jitter and loss: rollbacks, and the same bytes as offline", [] {
        for (const auto& [lat, jit, loss] : {std::tuple{4, 0, 0.0}, std::tuple{6, 3, 0.05}, std::tuple{8, 4, 0.15}}) {
            const auto o = play(1200, Link{.latency = lat, .jitter = jit, .loss = loss}, Link{.latency = lat, .jitter = jit, .loss = loss, .rng = {91}},
                                {.delay = 2, .max_rollback = 10});
            check(o.frames == 1200 && o.a == o.b && o.a == o.offline,
                  std::format("latency {}±{}, loss {}: {:016x} {:016x} offline {:016x}", lat, jit, loss, o.a, o.b, o.offline));
            check(o.rollbacks > 0 && !o.desync, std::format("{} rollbacks, desync {}", o.rollbacks, o.desync.value_or(-1)));
        }
    });

    s.test("a peer far behind is waited for: no frame runs past max_rollback", [] {
        const auto o = play(300, Link{.latency = 20}, Link{.latency = 20}, {.delay = 1, .max_rollback = 6});
        check(o.waited > 0 && o.a == o.b && o.a == o.offline, std::format("waited {} times", o.waited));
    });

    s.test("a game altered behind the session's back is named at the frame they parted", [] {
        const auto o = play(900, Link{.latency = 3}, Link{.latency = 3}, {.delay = 2, .checksum_every = 30}, true);
        check(o.desync.has_value(), "the checksums disagree");
        if (o.desync) check(*o.desync > 200 && *o.desync <= 240, std::format("parted at frame {} (the state at the start of the first checked frame after 200)", *o.desync));
    });

    return s.run(argc, argv);
}
