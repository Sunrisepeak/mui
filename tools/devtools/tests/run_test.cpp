// omui-devtools run: a program's output captured, its standard error with it when merged, in the order
// written; a status as a shell reports it; a missing program said so; the machine's share at least one.
import std;
import mcpplibs.mtest;
import mcpplibs.os;
import omui.devtools.check;
import omui.devtools.run;

using namespace mcpplibs::mtest;
namespace run = omui::devtools::run;

UTest cases{"devtools.run", [] {
    "output captured; standard error with it when merged, in order; the status as a shell reports it"_test = [] {
        if constexpr (mcpplibs::os::is_windows) return;
        const auto root = std::filesystem::current_path();
        const auto plain = run::program(root, "sh", {"-c", "echo out; echo err >&2; exit 3"});
        expect(plain.has_value() && eq(plain->status, 3) && eq(plain->out, std::string("out\n")));
        const auto merged = run::program(root, "sh", {"-c", "echo one; echo two >&2; echo three"}, {.merged = true});
        expect(merged.has_value() && eq(merged->out, std::string("one\ntwo\nthree\n"))) << (merged ? merged->out : merged.error());
        const auto only_error = run::program(root, "sh", {"-c", "echo alone >&2; exit 2"}, {.merged = true});
        expect(only_error.has_value() && eq(only_error->status, 2) && eq(only_error->out, std::string("alone\n")))
            << (only_error ? std::format("[{}] {:?}", only_error->status, only_error->out) : only_error.error());
    };

    "a missing program is said so; the machine's share is at least one compile"_test = [] {
        expect(!run::program(std::filesystem::current_path(), "omui-no-such-program", {}).has_value());
        expect(run::share_of_machine() >= 1);
    };
}};

UTest budget_cases{"devtools.check", [] {
    "bytes held to a budget: each one over it said, an unmeasured metric said, a bad file said"_test = [] {
        namespace check = omui::devtools::check;
        const std::map<std::string, std::uint64_t> measured{{"wasm.shipped", 400}, {"wasm.code", 100}};
        const auto over = check::over_budget(measured, R"({"budgets":[{"metric":"wasm.shipped","max":394},{"metric":"wasm.code","max":261},{"metric":"js.bytes","max":1}]})");
        expect(eq(over.size(), std::size_t{2}));
        expect(over.size() == 2 && over[0] == "wasm.shipped is 400 B, over its budget of 394 B" && over[1] == "js.bytes is not measured") << over.front();
        expect(eq(check::over_budget(measured, "not json").size(), std::size_t{1}));
    };
}};
