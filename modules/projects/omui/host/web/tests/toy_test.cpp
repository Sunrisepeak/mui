// The Toy layer off a Toy page: no SDK, every call refused in openas's
// words; the store's limits checked when a value is set, not when Toy
// refuses a flush; what was not written stays to be written.
import std;
import omui.host.web.toy;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
using namespace omui;

UTest cases{"host.web.toy", [] {

    "no SDK here: refused, in openas's words"_test = [] {
        expect(!toy::available());
        std::optional<toy::Error> e;
        toy::supports("submitScore", [&](std::expected<bool, toy::Error> r) {
            if (!r) e = r.error();
        });
        expect(e.has_value() && e->type == "unsupported" && e->openas_code() == "refused" && e->exit_code() == 1);
        // Toy's rate limit is "not ready" (exit code 3): the same request may succeed later.
        expect(eq(toy::Error{"http_error", 307044, ""}.exit_code(), 3));
        expect(eq(toy::Error{"invalid_param", 0, ""}.openas_code(), std::string_view("invalid-argument")));
    };

    "the store: limits on set, memory first, unwritten stays changed"_test = [] {
        toy::Store store;
        expect(store.set("best_score", "1200").has_value());
        expect(store.get("best_score") == std::optional<std::string>("1200"));
        expect(store.dirty());
        expect(!store.set("__private", "x").has_value()) << "keys starting __ are Toy's";
        expect(!store.set("has space", "x").has_value());
        expect(!store.set(std::string(129, 'k'), "x").has_value());
        expect(!store.set("big", std::string(1025, 'v')).has_value()) << "a value is at most 1024 bytes";
        for (int i = 0; i < 127; ++i) expect(store.set(std::format("k{}", i), "v").has_value());
        expect(!store.set("one_more", "v").has_value()) << "128 keys a Toy";
        std::optional<toy::Error> e;
        store.flush([&](std::expected<void, toy::Error> r) {
            if (!r) e = r.error();
        });
        expect(e.has_value()) << "no SDK: not written";
        expect(store.dirty()) << "what was not written is still to be written";
    };

    "leaderboards: a score out of range is refused before Toy hears it"_test = [] {
        std::optional<toy::Error> e;
        toy::submit_score(1 << 25, 1, [&](std::expected<std::int64_t, toy::Error> r) {
            if (!r) e = r.error();
        });
        expect(e && e->type == "invalid_param");
        e.reset();
        toy::submit_score(100, 4, [&](std::expected<std::int64_t, toy::Error> r) {
            if (!r) e = r.error();
        });
        expect(e && e->type == "invalid_param") << "boards 1, 2, 3";
    };
}};
