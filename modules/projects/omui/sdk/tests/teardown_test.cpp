// AppSpec::teardown: what an application's setup made that holds on to the
// runtime (a player and its timers) lets go before the runtime goes — the
// runtime still there when it does — however the run ends.
import std;
import omui;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

UTest cases{"sdk.teardown", [] {
    "a headless run calls teardown with its runtime still there"_test = [] {
        RunOptions options;
        options.headless = true;
        options.frames = 1;
        options.level = 0;
        AppSpec app;
        std::uint64_t timer = 0;
        bool torn = false;
        app.setup = [&](Runtime& rt) { timer = rt.after(seconds(10), [] {}); };
        app.teardown = [&](Runtime& rt) {
            rt.cancel(timer);  // the runtime's own call: it is still there
            torn = true;
        };
        expect(eq(run(std::move(app), options), 0));
        expect(torn);
    };
}};
