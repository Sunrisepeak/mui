// Entrances and exits defined once (hanim.core:presence): the catalog the
// specification names, each presence's defaults, its pose and opacity over
// its progress, options over the defaults, the errors a recipe reports, and
// two presences at once.
import std;
import hanim.core;
import mlibs.testing;

using namespace mlibs::testing;

int main(int argc, char** argv) {
    Suite s{"hanim.presence"};

    s.test("the catalog: entrances then exits, each with the defaults its recipe has always had", [] {
        using namespace hanim;
        const auto names = presence_names();
        check_eq(names.size(), std::size_t{21});
        for (const auto name : names) {
            const auto made = presence(name);
            check(made.has_value(), std::string(name));
            if (made) check((made->kind == PresenceKind::enter) == name.ends_with("_in"), std::format("{} is an entrance or an exit", name));
        }
        const auto rise = *presence("rise_in");
        check(rise.away.offset == Vec2{0, 24} && rise.duration == seconds(.4) && rise.ease == ease::out_cubic && rise.fades);
        const auto pop = *presence("pop_out");
        check(pop.away.scale == Vec2{.3, .3} && pop.duration == seconds(.35) && pop.ease == ease::in_back);
        const auto spin = *presence("spin_in");
        check(spin.away.rotation == -1.2 && spin.away.scale == Vec2{.6, .6});
        check(presence("write_in")->writes && !presence("write_in")->fades);
        check(presence("drop_in")->drop == 160 && !presence("drop_in")->fades);
        // The UI's quiet one: a little smaller, no overshoot.
        const auto settle = *presence("settle_in");
        check(settle.away.scale == Vec2{.96, .96} && settle.ease == ease::out_cubic);
    });

    s.test("the pose and the opacity over the progress: an entrance from away to rest, an exit the other way, fading within its window", [] {
        using namespace hanim;
        const auto rise = *presence("rise_in", {.distance = 8, .fade = .3});
        check_bits(rise.pose(0).offset.y, 8.0);
        check_bits(rise.pose(1).offset.y, 0.0);
        check_bits(rise.pose(.5).offset.y, 4.0);
        check_bits(rise.opacity(.3), 0.0);
        check_bits(rise.opacity(1), 1.0);
        check(rise.opacity(.65) > .49 && rise.opacity(.65) < .51);
        const auto gone = *presence("settle_out", {.scale = .9, .fade = .6});
        check_bits(gone.pose(0).scale.x, 1.0);
        check_bits(gone.pose(1).scale.x, .9);
        check_bits(gone.opacity(0), 1.0);
        check_bits(gone.opacity(.6), 0.0);
        check_bits(gone.opacity(1), 0.0);
        const auto written = *presence("write_in");
        check_bits(written.opacity(0), 1.0);
    });

    s.test("options over the defaults; what a recipe would refuse is refused", [] {
        using namespace hanim;
        const auto slow = *presence("fade_in", {.duration = seconds(1), .ease = ease::linear});
        check(slow.duration == seconds(1) && slow.ease == ease::linear);
        check(presence("nope").error() == PresenceError::unknown_presence);
        check(presence("fade_in", {.duration = seconds(-1)}).error() == PresenceError::invalid_time);
        check(presence("fade_in", {.ease = Ease{static_cast<EaseKind>(255)}}).error() == PresenceError::invalid_ease);
        check(presence("rise_in", {.distance = std::numeric_limits<double>::infinity()}).error() == PresenceError::invalid_value);
        check(presence("drop_in", {.height = 0}).error() == PresenceError::invalid_value);
        check(!describe(PresenceError::invalid_value).empty());
    });

    s.test("two at once: a rise that also settles", [] {
        using namespace hanim;
        const auto both = together(*presence("rise_in", {.distance = 8, .fade = .3}), *presence("settle_in", {.scale = .96}));
        check(both.has_value());
        if (!both) return;
        check(both->away.offset == Vec2{0, 8} && both->away.scale == Vec2{.96, .96} && both->fade_from == .3);
        check(!together(*presence("rise_in"), *presence("rise_out")).has_value());
    });

    return s.run(argc, argv);
}
