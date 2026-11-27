// Glade's simulation: the same verbs are the same bits, a copy is a branch, and each task has a solution the laws
// give — with the obvious wrong try failing for the reason `explain` says.
import std;
import omui.showcase.glade.sim;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"glade.sim", [] {
    "the same verbs are the same bits; a copy taken part way is a branch that agrees, and goes its own way when poked"_test = [] {
        expect(fatal(glade::program() != nullptr)) << "the standard definition with Glade's pack prepares";
        const auto play = [] {
            glade::State state = glade::start(glade::level::glade());
            (void)glade::torch(state, {-3, 0.1f, -3}, 2);
            glade::run(state, 60);
            return state;
        };
        glade::State one = play(), other = play();
        expect(eq(glade::checksum(one), glade::checksum(other)));
        glade::State branch = one;
        glade::run(one, 60), glade::run(other, 60), glade::run(branch, 60);
        expect(eq(glade::checksum(one), glade::checksum(other)) && eq(glade::checksum(one), glade::checksum(branch))) << "a branch agrees";
        glade::State poked = one;
        glade::pour(poked, {-2.8f, 0.3f, -3}, 27);
        glade::run(one, 30), glade::run(poked, 30);
        expect(ne(glade::checksum(one), glade::checksum(poked))) << "and goes its own way";
        std::println("two seconds of the campfire: {:016x}, the same twice and on a branch", glade::checksum(one));
    };

    "across: the crate on the far bank is lit from the near bank — the fire walks the grass and the footbridge over the stream; the agent's explanation runs back to the torch"_test = [] {
        const auto tasks = glade::tasks();
        const glade::Task& across = tasks[0];
        expect(!across.may_act_at({5.5f, 0.4f, 0})) << "a torch to the crate itself is not allowed: it is across the stream";
        glade::State state = glade::start(glade::level::glade());
        expect(across.may_act_at({-3, 0.15f, 0}) && glade::torch(state, {-3, 0.15f, 0}, 1)) << "the far end of the near grass, lit";
        int done_at = 0;
        float hottest = 0;
        for (int step = 1; step <= 2400 && !done_at; ++step) {
            glade::step(state);
            hottest = std::max(hottest, glade::seen(state, "crate")->kelvin);
            if (across.done(state)) done_at = step;
        }
        std::println("across: the crate at most {:.0f} K", hottest);
        std::string burned;
        for (const glade::Seen& thing : glade::observe(state))
            if (thing.burning || thing.gone) burned += std::format(" {}{}", thing.name, thing.burning ? "*" : "");
        std::println("across: the crate caught at {:.1f} s; burned (* still alight):{}; why:\n{}", done_at / 60.0, burned, glade::explain(state, "crate"));
        expect(gt(done_at, 0)) << "the crate caught";
        const auto chain = glade::chain(state, "crate");
        const bool through_bridge = std::ranges::any_of(chain, [&](const glade::matter::Explained& link) {
            const auto name = glade::name_of(state, glade::rigid3::body_of(link.who));
            return name && name->starts_with("plank");
        });
        expect(through_bridge) << "by way of the footbridge";
        expect(!chain.empty() && chain.back().cause.kind == glade::matter::CauseKind::applied) << "back to the torch";
    };

    "campfire: after the rain a short torch does not light the wet logs — wet is harder to light; held long enough it dries them and they catch"_test = [] {
        const auto after_rain = [] {
            glade::State state = glade::start(glade::level::glade());
            glade::rain(state, true);
            glade::run(state, 600);
            glade::rain(state, false);
            return state;
        };
        const auto lit = [](glade::State& state, float seconds) {
            (void)glade::torch(state, {-2.8f, 0.1f, -3}, seconds);
            for (int step = 0; step < 300; ++step) glade::step(state);
            return glade::tasks()[1].done(state);
        };
        glade::State wet = after_rain();
        std::println("after ten seconds of rain the middle log's wetness {:.0f}", glade::seen(wet, "log 2")->wetness);
        expect(gt(glade::seen(wet, "log 2")->wetness, 10.0f)) << "the rain wet the logs";
        glade::State brief = wet, held = wet;
        const bool brief_lit = lit(brief, 0.3f), held_lit = lit(held, 3);
        std::println("a torch for 0.3 s: lit {}; for 3 s: lit {}; why log 2 burns:\n{}", brief_lit, held_lit, glade::explain(held, "log 2"));
        expect(!brief_lit) << "a moment's torch does not light wet wood";
        expect(held_lit) << "held, it dries and catches";
    };

    "ice: the block of ice melts by the campfire — a torch to the logs, a push toward them"_test = [] {
        glade::State state = glade::start(glade::level::glade());
        (void)glade::torch(state, {-2.8f, 0.1f, -3}, 2);
        glade::run(state, 120);
        expect(glade::push(state, "ice", {-200, 0, 0})) << "pushed toward the fire";
        int done_at = 0;
        for (int step = 1; step <= 1800 && !done_at; ++step) {
            glade::step(state);
            if (glade::tasks()[2].done(state)) done_at = step;
        }
        std::println("ice: melted {:.1f} s after the push; why:\n{}", done_at / 60.0, glade::explain(state, "ice"));
        expect(gt(done_at, 0)) << "it melted";
    };

    "storm: lightning strikes nothing while nothing that conducts stands open, and an iron rod stood in the kindling takes it — what its heat does next is R11's (heat by surface and mass)"_test = [] {
        const auto tasks = glade::tasks();
        const glade::Task& storm = tasks[3];
        expect(std::ranges::find(storm.verbs, glade::Verb::torch) == storm.verbs.end()) << "no torch";
        glade::State state = glade::start(glade::level::glade());
        // What is seen of the valley: the ground's heights; the crate open to the sky, a trunk under its crown not.
        const glade::Level level = glade::level::glade();
        expect(eq(level.ground.height(-3, 0), 0.0f) && lt(level.ground.height(1, 0), -0.7f) && gt(level.ground.height(-9.5f, 0), 1.0f)) << "banks, the stream's channel, the hill";
        expect(glade::seen(state, "crate")->open && !glade::seen(state, "near tree trunk")->open && glade::seen(state, "near tree crown")->open);
        expect(!glade::strike(state).has_value()) << "wood, leaves and stone do not draw it";
        expect(glade::drop(state, {.name = "rod", .at = {-2.8f, 1.0f, -2.37f}, .half = {0.03f, 1.0f, 0.03f}, .material = "iron"})) << "a rod stood in the kindling at the middle log's end";
        glade::run(state, 30);
        const auto struck = glade::strike(state);
        expect(struck.has_value() && *struck == "rod") << struck.value_or("nothing");
        expect(gt(glade::seen(state, "rod")->kelvin, 1000.0f)) << "the strike's heat in the rod";
        // Measured, not yet asserted (R11): the rod 1093 K as struck, back to the air's warmth within 20 s, the
        // kindling against it at most 383 K of the 470 it catches at — iron that hot keeps for minutes and lights
        // dry leaves in seconds.
        float kindling_hottest = 0;
        for (int step = 1; step <= 1200; ++step) glade::step(state), kindling_hottest = std::max(kindling_hottest, glade::seen(state, "kindling")->kelvin);
        std::println("storm: the rod struck; 20 s on, the rod {:.0f} K, the kindling at most {:.0f} K (catches at 470)", glade::seen(state, "rod")->kelvin, kindling_hottest);
    };
}};
