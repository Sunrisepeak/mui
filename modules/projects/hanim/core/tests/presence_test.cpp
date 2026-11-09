// Entrances and exits defined once (hanim.core:presence): the catalog the
// specification names, each presence's defaults, its pose and opacity over
// its progress, options over the defaults, the errors a recipe reports, and
// two presences at once.
import std;
import hanim.core;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"hanim.presence", [] {

    "the catalog: entrances then exits, each with the defaults its recipe has always had"_test = [] {
        using namespace hanim;
        const auto names = presence_names();
        expect(eq(names.size(), std::size_t{21}));
        for (const auto name : names) {
            const auto made = presence(name);
            expect(made.has_value()) << std::string(name);
            if (made) expect((made->kind == PresenceKind::enter) == name.ends_with("_in")) << std::format("{} is an entrance or an exit", name);
        }
        const auto rise = *presence("rise_in");
        expect(rise.away.offset == Vec2{0, 24} && rise.duration == seconds(.4) && rise.ease == ease::out_cubic && rise.fades);
        const auto pop = *presence("pop_out");
        expect(pop.away.scale == Vec2{.3, .3} && pop.duration == seconds(.35) && pop.ease == ease::in_back);
        const auto spin = *presence("spin_in");
        expect(spin.away.rotation == -1.2 && spin.away.scale == Vec2{.6, .6});
        expect(presence("write_in")->writes && !presence("write_in")->fades);
        expect(presence("drop_in")->drop == 160 && !presence("drop_in")->fades);
        // The UI's quiet one: a little smaller, no overshoot.
        const auto settle = *presence("settle_in");
        expect(settle.away.scale == Vec2{.96, .96} && settle.ease == ease::out_cubic);
    };

    "the pose and the opacity over the progress: an entrance from away to rest, an exit the other way, fading within its window"_test = [] {
        using namespace hanim;
        const auto rise = *presence("rise_in", {.distance = 8, .fade = .3});
        expect(eq(std::bit_cast<std::uint64_t>(double(rise.pose(0).offset.y)), std::bit_cast<std::uint64_t>(double(8.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(rise.pose(1).offset.y)), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(rise.pose(.5).offset.y)), std::bit_cast<std::uint64_t>(double(4.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(rise.opacity(.3))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(rise.opacity(1))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(rise.opacity(.65) > .49 && rise.opacity(.65) < .51);
        const auto gone = *presence("settle_out", {.scale = .9, .fade = .6});
        expect(eq(std::bit_cast<std::uint64_t>(double(gone.pose(0).scale.x)), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(gone.pose(1).scale.x)), std::bit_cast<std::uint64_t>(double(.9))));
        expect(eq(std::bit_cast<std::uint64_t>(double(gone.opacity(0))), std::bit_cast<std::uint64_t>(double(1.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(gone.opacity(.6))), std::bit_cast<std::uint64_t>(double(0.0))));
        expect(eq(std::bit_cast<std::uint64_t>(double(gone.opacity(1))), std::bit_cast<std::uint64_t>(double(0.0))));
        const auto written = *presence("write_in");
        expect(eq(std::bit_cast<std::uint64_t>(double(written.opacity(0))), std::bit_cast<std::uint64_t>(double(1.0))));
    };

    "options over the defaults; what a recipe would refuse is refused"_test = [] {
        using namespace hanim;
        const auto slow = *presence("fade_in", {.duration = seconds(1), .ease = ease::linear});
        expect(slow.duration == seconds(1) && slow.ease == ease::linear);
        expect(presence("nope").error() == PresenceError::unknown_presence);
        expect(presence("fade_in", {.duration = seconds(-1)}).error() == PresenceError::invalid_time);
        expect(presence("fade_in", {.ease = Ease{static_cast<EaseKind>(255)}}).error() == PresenceError::invalid_ease);
        expect(presence("rise_in", {.distance = std::numeric_limits<double>::infinity()}).error() == PresenceError::invalid_value);
        expect(presence("drop_in", {.height = 0}).error() == PresenceError::invalid_value);
        expect(!describe(PresenceError::invalid_value).empty());
    };

    "two at once: a rise that also settles"_test = [] {
        using namespace hanim;
        const auto both = together(*presence("rise_in", {.distance = 8, .fade = .3}), *presence("settle_in", {.scale = .96}));
        expect(both.has_value());
        if (!both) return;
        expect(both->away.offset == Vec2{0, 8} && both->away.scale == Vec2{.96, .96} && both->fade_from == .3);
        expect(!together(*presence("rise_in"), *presence("rise_out")).has_value());
    };
}};
