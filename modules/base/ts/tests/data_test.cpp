// Data modules (mlibs.ts:data): TypeScript in, JSON out — keys in the
// module's order, helpers imported from its own files, computed values; and
// what makes it data that gives the same answer every time and stops: no
// clock, no random, data only (each problem at its JSON pointer), imports
// inside its directory, a loop that does not end stopped by its time.
import std;
import mcpplibs.mtest;
import mlibs.ts;

using namespace mcpplibs::mtest;
namespace ts = mlibs::ts;

namespace {

// A directory of the case's own (mtest removes it when the case ends).
std::filesystem::path scratch(std::string_view name) {
    auto dir = temp_dir() / name;
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view text) { std::ofstream(file, std::ios::binary) << text; }

// The problem of evaluating `source` as a module in `dir`, or "" when it evaluated.
// The kinds of the cases' define module, "test/define": an owner's, given with the options.
constexpr std::array<ts::DefinedKind, 2> kinds{{{"contract", 1}, {"config", 2}}};
ts::DataOptions defined(ts::DataOptions options = {}) {
    options.modules.push_back({"test/define", ts::define_module(kinds)});
    return options;
}

std::string problem_of(const std::filesystem::path& dir, std::string_view source, ts::DataOptions options = {}) {
    write(dir / "case.ts", source);
    auto data = ts::evaluate(dir / "case.ts", defined(std::move(options)));
    return data ? std::string() : data.error().text();
}

}  // namespace

UTest cases{"mlibs.ts data", [] {
    "a contract in TypeScript: its JSON, keys in order, helpers and computed values"_test = [] {
        const auto dir = scratch("contract");
        write(dir / "shared.ts", R"ts(
export const motion = (speed: "fast" | "slow"): string => `motion.${speed}`;
export const enumOf = <T extends string>(values: readonly T[], fallback: T, doc: string) => ({ type: "enum", values, default: fallback, doc });
)ts");
        write(dir / "caption_button.ts", R"ts(
import { defineContract } from "test/define";
import { enumOf, motion } from "./shared";

const kinds = ["minimize", "maximize", "restore", "close"] as const;
export default defineContract({
  caption_button: {
    role: "button",
    spec: "0.1",
    doc: "A window frame's button: minimize, maximize, restore or close.",
    props: {
      kind: enumOf(kinds, "close", "What it does."),
      active: { type: "bool", default: true, doc: "Whether its window is active." },
    },
    keys: { "Enter|Space": "activate" },
    motion: { hover: motion("fast") },
    sizes: [16, 1.5, -2],
  },
});
)ts");
        auto data = ts::evaluate(dir / "caption_button.ts", defined());
        expect(data.has_value()) << (data ? "" : data.error().text());
        if (!data) return;
        expect(eq(data->kind, std::string("contract")));
        expect(eq(data->json,
                  std::string(R"({"caption_button":{"role":"button","spec":"0.1","doc":"A window frame's button: minimize, maximize, restore or close.",)"
                              R"("props":{"kind":{"type":"enum","values":["minimize","maximize","restore","close"],"default":"close","doc":"What it does."},)"
                              R"("active":{"type":"bool","default":true,"doc":"Whether its window is active."}},"keys":{"Enter|Space":"activate"},)"
                              R"("motion":{"hover":"motion.fast"},"sizes":[16,1.5,-2]}})")));
        const auto again = ts::evaluate(dir / "caption_button.ts", defined());
        expect(again && again->json == data->json) << "the same answer every time";
    };

    "a define module: a function per kind, each giving { kind, format, value }; the snapshot's canonical form"_test = [] {
        const auto dir = scratch("kinds");
        for (const auto& kind : kinds) {
            std::string name(kind.name);
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            write(dir / "kind.ts", std::format("import {{ define{0} }} from \"test/define\";\nexport default define{0}({{ a: 1 }});\n", name));
            auto data = ts::evaluate(dir / "kind.ts", defined());
            expect(data && data->kind == kind.name && data->json == R"({"a":1})") << (data ? data->kind : data.error().text());
            const auto format = std::format("{}@{}", kind.name, kind.major);
            expect(data && data->format == format) << "the define function writes the format";
            expect(data && data->snapshot() == std::format("{{\"$format\":\"{}\",\"{}\":{{\"a\":1}}}}\n", format, kind.name)) << (data ? data->snapshot() : "");
        }
        write(dir / "other.ts", "import { defineMissing } from \"test/define\";\nexport default defineMissing({});\n");
        const auto other = ts::evaluate(dir / "other.ts", defined());
        expect(!other && other.error().text().contains("defineMissing")) << "a kind the module does not define is no import";
    };

    "data only: each problem at its JSON pointer"_test = [] {
        const auto dir = scratch("data-only");
        const std::array<std::pair<std::string_view, std::string_view>, 6> refused{{
            {"export default defineContract({ a: { b: () => 1 } });", "/a/b: a function is not data"},
            {"export default defineContract({ list: [1, undefined] });", "/list/1: undefined is not data"},
            {"export default defineContract({ x: 0 / 0 });", "/x: NaN is not a number data holds"},
            {"const loop: any = {}; loop.self = loop; export default defineContract({ loop });", "/loop/self: the value holds itself"},
            {"export default defineContract({ when: new Map() });", "/when: a Map is not plain data"},
            {"export default { a: 1 };", "the default export is not a define…() result"},
        }};
        for (const auto& [body, says] : refused) {
            const auto problem = problem_of(dir, std::format("import {{ defineContract }} from \"test/define\";\n{}\n", body));
            expect(problem.contains(says)) << std::format("{} → {}", body, problem);
        }
        const auto shared = problem_of(dir, "import { defineContract } from \"test/define\";\nconst point = { x: 1 };\nexport default defineContract({ a: point, b: point });\n");
        expect(shared.empty()) << "a value used twice is data: " << shared;
    };

    "the same answer every time: no clock, no random"_test = [] {
        const auto dir = scratch("deterministic");
        expect(problem_of(dir, "export default Date.now();").contains("Date is not defined"));
        expect(problem_of(dir, "export default Math.random();").contains("Math.random is not here"));
        expect(problem_of(dir, "export default new WeakRef({});").contains("WeakRef is not defined"));
    };

    "imports: openmui/define and its own files, inside its directory"_test = [] {
        const auto dir = scratch("imports");
        std::filesystem::create_directories(dir / "inner");
        write(dir / "outside.ts", "export const x = 1;\n");
        const ts::DataOptions inner = defined({.root = dir / "inner"});
        write(dir / "inner" / "case.ts", "import { x } from \"../outside\";\nexport default x;\n");
        auto escaped = ts::evaluate(dir / "inner" / "case.ts", inner);
        expect(!escaped && escaped.error().text().contains("its own files inside its directory, not")) << (escaped ? escaped->json : escaped.error().text());
        expect(problem_of(dir, "import fs from \"node:fs\";\nexport default fs;").contains("not `node:fs`"));
        expect(problem_of(dir, "import { y } from \"./missing\";\nexport default y;").contains("no module"));
    };

    "it stops: a loop that does not end runs out of its time; a problem names the file and line"_test = [] {
        const auto dir = scratch("bounded");
        const auto start = std::chrono::steady_clock::now();
        const auto endless = problem_of(dir, "while (true) {}\nexport default 1;", {.time = std::chrono::milliseconds(100)});
        expect(endless.contains("ran out of its time")) << endless;
        expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
        const auto thrown = problem_of(dir, "import { defineContract } from \"test/define\";\n\nthrow new Error(\"no view yet\");\n");
        expect(thrown.contains("case.ts") && thrown.contains("no view yet") && thrown.contains(":3")) << thrown;
        const auto typed = problem_of(dir, "enum E { A }\nexport default 1;");
        expect(typed.contains("case.ts:1:1: `enum` is not erasable")) << typed;
    };
}};
