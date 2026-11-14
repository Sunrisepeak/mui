// Data modules (omui.lang.ts:data): TypeScript in, JSON out — keys in the
// module's order, helpers imported from its own files, computed values; and
// what makes it data that gives the same answer every time and stops: no
// clock, no random, data only (each problem at its JSON pointer), imports
// inside its directory, a loop that does not end stopped by its time.
import std;
import mcpplibs.mtest;
import omui.lang.ts;

using namespace mcpplibs::mtest;
namespace ts = omui::lang::ts;

namespace {

// A directory of the case's own (mtest removes it when the case ends).
std::filesystem::path scratch(std::string_view name) {
    auto dir = temp_dir() / name;
    std::filesystem::create_directories(dir);
    return dir;
}

void write(const std::filesystem::path& file, std::string_view text) { std::ofstream(file, std::ios::binary) << text; }

// The problem of evaluating `source` as a module in `dir`, or "" when it evaluated.
std::string problem_of(const std::filesystem::path& dir, std::string_view source, const ts::DataOptions& options = {}) {
    write(dir / "case.config.omui.ts", source);
    auto data = ts::evaluate(dir / "case.config.omui.ts", options);
    return data ? std::string() : data.error();
}

}  // namespace

UTest cases{"lang.ts data", [] {
    "a contract in TypeScript: its JSON, keys in order, helpers and computed values"_test = [] {
        const auto dir = scratch("contract");
        write(dir / "shared.ts", R"ts(
export const motion = (speed: "fast" | "slow"): string => `motion.${speed}`;
export const enumOf = <T extends string>(values: readonly T[], fallback: T, doc: string) => ({ type: "enum", values, default: fallback, doc });
)ts");
        write(dir / "caption_button.contract.omui.ts", R"ts(
import { defineContract } from "openmui/define";
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
        auto data = ts::evaluate(dir / "caption_button.contract.omui.ts");
        expect(data.has_value()) << (data ? "" : data.error());
        if (!data) return;
        expect(eq(data->kind, std::string("contract")));
        expect(eq(data->json,
                  std::string(R"({"caption_button":{"role":"button","spec":"0.1","doc":"A window frame's button: minimize, maximize, restore or close.",)"
                              R"("props":{"kind":{"type":"enum","values":["minimize","maximize","restore","close"],"default":"close","doc":"What it does."},)"
                              R"("active":{"type":"bool","default":true,"doc":"Whether its window is active."}},"keys":{"Enter|Space":"activate"},)"
                              R"("motion":{"hover":"motion.fast"},"sizes":[16,1.5,-2]}})")));
        const auto again = ts::evaluate(dir / "caption_button.contract.omui.ts");
        expect(again && again->json == data->json) << "the same answer every time";
    };

    "every define function, by kind"_test = [] {
        const auto dir = scratch("kinds");
        for (const auto kind : ts::data_kinds) {
            std::string name(kind);
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            const bool spec = std::ranges::contains(ts::spec_kinds, kind);  // D-157: the spec's formats, the implementation's
            write(dir / "kind.ts", std::format("import {{ define{0} }} from \"{1}/define\";\nexport default define{0}({{ a: 1 }});\n", name,
                                               spec ? "openmui" : "omui"));
            auto data = ts::evaluate(dir / "kind.ts");
            expect(data && data->kind == kind && data->json == R"({"a":1})") << (data ? data->kind : data.error());
        }
    };

    "the spec's formats from openmui/define, the implementation's from omui/define (D-157)"_test = [] {
        const auto dir = scratch("layers");
        const auto config = problem_of(dir, "import { defineConfig } from \"openmui/define\";\nexport default defineConfig({});\n");
        expect(config.contains("defineConfig")) << "the spec's module has no implementation config: " << config;
        expect(problem_of(dir, "import { defineConfig } from \"omui/define\";\nexport default defineConfig({ agent: {} });\n").empty());
        expect(!problem_of(dir, "import { defineContract } from \"omui/define\";\nexport default defineContract({});\n").empty())
            << "a contract is the spec's format";
    };

    "data only: each problem at its JSON pointer"_test = [] {
        const auto dir = scratch("data-only");
        const std::array<std::pair<std::string_view, std::string_view>, 6> refused{{
            {"export default defineView({ a: { b: () => 1 } });", "/a/b: a function is not data"},
            {"export default defineView({ list: [1, undefined] });", "/list/1: undefined is not data"},
            {"export default defineView({ x: 0 / 0 });", "/x: NaN is not a number data holds"},
            {"const loop: any = {}; loop.self = loop; export default defineView({ loop });", "/loop/self: the value holds itself"},
            {"export default defineView({ when: new Map() });", "/when: a Map is not plain data"},
            {"export default { a: 1 };", "the default export is not a define…() result"},
        }};
        for (const auto& [body, says] : refused) {
            const auto problem = problem_of(dir, std::format("import {{ defineView }} from \"openmui/define\";\n{}\n", body));
            expect(problem.contains(says)) << std::format("{} → {}", body, problem);
        }
        const auto shared = problem_of(dir, "import { defineView } from \"openmui/define\";\nconst point = { x: 1 };\nexport default defineView({ a: point, b: point });\n");
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
        const ts::DataOptions inner{.root = dir / "inner"};
        write(dir / "inner" / "case.ts", "import { x } from \"../outside\";\nexport default x;\n");
        auto escaped = ts::evaluate(dir / "inner" / "case.ts", inner);
        expect(!escaped && escaped.error().contains("its own files inside its directory, not")) << (escaped ? escaped->json : escaped.error());
        expect(problem_of(dir, "import fs from \"node:fs\";\nexport default fs;").contains("not `node:fs`"));
        expect(problem_of(dir, "import { y } from \"./missing\";\nexport default y;").contains("no module"));
    };

    "it stops: a loop that does not end runs out of its time; a problem names the file and line"_test = [] {
        const auto dir = scratch("bounded");
        const auto start = std::chrono::steady_clock::now();
        const auto endless = problem_of(dir, "while (true) {}\nexport default 1;", {.time = std::chrono::milliseconds(100)});
        expect(endless.contains("ran out of its time")) << endless;
        expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
        const auto thrown = problem_of(dir, "import { defineView } from \"openmui/define\";\n\nthrow new Error(\"no view yet\");\n");
        expect(thrown.contains("case.config.omui.ts") && thrown.contains("no view yet") && thrown.contains(":3")) << thrown;
        const auto typed = problem_of(dir, "enum E { A }\nexport default 1;");
        expect(typed.contains("case.config.omui.ts:1:1: `enum` is not erasable")) << typed;
    };
}};
