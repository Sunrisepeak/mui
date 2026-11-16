// omui's data formats (omui.lang.ts:define): every kind's define function, the spec's from "openmui/define" and
// the implementation's from "omui/define" (D-157). How a data module is evaluated is mlibs.ts's (its tests).
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
std::string problem_of(const std::filesystem::path& dir, std::string_view source) {
    write(dir / "case.config.xdef.ts", source);
    auto data = ts::evaluate(dir / "case.config.xdef.ts");
    return data ? std::string() : data.error().text();
}

}  // namespace

UTest cases{"lang.ts define", [] {
    "every define function, by kind"_test = [] {
        const auto dir = scratch("kinds");
        for (const auto kind : ts::data_kinds) {
            std::string name(kind);
            name[0] = static_cast<char>(name[0] - 'a' + 'A');
            const bool spec = std::ranges::contains(ts::spec_kinds, kind);  // D-157: the spec's formats, the implementation's
            write(dir / "kind.ts", std::format("import {{ define{0} }} from \"{1}/define\";\nexport default define{0}({{ a: 1 }});\n", name,
                                               spec ? "openmui" : "omui"));
            auto data = ts::evaluate(dir / "kind.ts");
            expect(data && data->kind == kind && data->json == R"({"a":1})") << (data ? data->kind : data.error().text());
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

}};
