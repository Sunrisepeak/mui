// TypeScript's declarations for a type checker (W1 step 5): what a runtime
// registered — the official components, a native module — written as
// `openmui.d.ts` beside the define functions and tsc's configuration; with
// tsc on PATH (TypeScript 7, `xim:typescript`), a correct application passes
// and each mistake is reported where it is.
import std;
import mcpplibs.os;
import mcpplibs.mtest;
import omui.abi;
import omui.lang.js;
import omui.ui.base;
import omui.native;
import omui.native.fuzzy;

using namespace mcpplibs::mtest;
using namespace omui::abi;
namespace fs = std::filesystem;

namespace {

void collect(void* user, const char* data, std::size_t size) { static_cast<std::string*>(user)->append(data, size); }

void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

// The declarations of a headless runtime with the official components and fuzzy, for an application at `root`.
std::vector<fs::path> declared(const fs::path& root) {
    omui::register_base_components();
    if (!omui::native::find("fuzzy")) expect(omui::natives::fuzzy::define().has_value());
    OmuiRuntimeCreateInfo info{};
    info.type = OMUI_STRUCTURE_TYPE_RUNTIME_CREATE_INFO;
    info.spec_version = OMUI_SPEC_VERSION;
    info.headless = 1;
    OmuiRuntime rt = nullptr;
    expect(eq(omuiCreateRuntime(&info, &rt), OMUI_SUCCESS));
    std::string catalog, natives;
    (void)omuiCatalog(rt, collect, &catalog);
    (void)omuiNativeCatalog(rt, collect, &natives);
    (void)omuiDestroyRuntime(rt);
    auto files = omui::lang::js::typescript().declare(catalog, natives, root, root / ".omui" / "types");
    expect(files.has_value()) << (files ? "" : files.error());
    return files ? *files : std::vector<fs::path>{};
}

// tsc over the application at `root`: what it reported (its standard output;
// a launcher may say things of its own on the other), "" when it passed;
// nothing without tsc.
std::optional<std::string> tsc(const fs::path& root) {
    const auto program = mcpplibs::os::env::find_executable("tsc");
    if (!program) return std::nullopt;
    mcpplibs::os::process::spawn_options options;
    options.program = *program;
    options.args = {"-p", mcpplibs::os::fs::utf8(root / ".omui" / "types" / "tsconfig.json"), "--pretty", "false"};
    options.cwd = root;
    auto ran = mcpplibs::os::process::run(std::move(options));
    if (!ran) return std::string("tsc did not start");
    if (ran->status.shell_code() != 0 && ran->out.empty()) return std::format("tsc failed: {}", ran->err);
    return ran->out;
}

}  // namespace

UTest cases{"lang.js declare", [] {
    "the files: the API and every registered builder, the natives, the define functions, tsc's configuration"_test = [] {
        const fs::path root = temp_dir() / "app";
        const auto files = declared(root);
        expect(eq(files.size(), std::size_t{5}));
        const std::string api = read(root / ".omui" / "types" / "openmui.d.ts");
        for (const std::string_view part : {"export declare function signal<T>(", "export declare function mount(", "export interface NodeProps {",
                                            "export declare function button(props?: ButtonProps", "export declare function textBox(props?: TextBoxProps",
                                            "In the spec's registry: portable"})
            expect(api.contains(part)) << part;
        expect(read(root / ".omui" / "types" / "natives.d.ts").contains(R"(declare module "openmui:native/fuzzy")"));
        expect(read(root / ".omui" / "types" / "openmui-define.d.ts").contains("export declare function defineContract("));
        expect(read(root / ".omui" / "types" / "omui-define.d.ts").contains("export declare function defineConfig("));
        const std::string config = read(root / ".omui" / "types" / "tsconfig.json");
        expect(config.contains(R"("erasableSyntaxOnly": true)") && config.contains(R"("../../**/*.ts")") && config.contains(R"("./openmui.d.ts")")) << config;
    };

    "tsc: a correct application passes; a misspelt property, a wrong choice, a signal of the wrong type are reported where they are"_test = [] {
        const fs::path root = temp_dir() / "checked";
        write(root / "app" / "main.ts", R"ts(import { button, column, fmt, mount, signal, text, textBox, verb, when } from "openmui";
import { fuzzy } from "openmui:native/fuzzy";
const count = signal(0), name = signal("");
verb("count.add", { effect: "reversible", summary: "One more" }, () => count.update((n) => n + 1));
mount("main", column({ id: "page", gap: 8, padding: 16 }, [
  text({ id: "total", text: fmt("{} clicks", count) }),
  textBox({ id: "name", text: name, label: "Name" }),
  button({ id: "add", text: "Add", variant: "primary", onActivated: () => count.update((n) => n + 1) }),
  when(count.gt(3), text({ id: "many", text: "Many" })),
]));
const best = fuzzy.rankSync("ab", ["abc", "xyz"]);
export const first: number = best.length ? best[0].index : -1;
)ts");
        write(root / "omui.config.ts", R"ts(import { defineConfig } from "omui/define";
export default defineConfig({ agent: { release: "dual-user" }, record: { development: "full" } });
)ts");
        (void)declared(root);
        const auto passed = tsc(root);
        if (!passed) skip("tsc is not installed here (xlings install xim:typescript)");
        expect(passed->empty()) << *passed;

        write(root / "app" / "wrong.ts", R"ts(import { button, column, signal, textBox } from "openmui";
const count = signal(0);
column({ widht: 10 }, [button({ variant: "huge" }), textBox({ text: count })]);
)ts");
        const auto failed = tsc(root).value_or("");
        for (const std::string_view part : {"wrong.ts(3,10): error TS2561", "wrong.ts(3,33): error TS2769", "wrong.ts(3,63): error TS2769"})
            expect(failed.contains(part)) << std::format("{}\n---\n{}", part, failed);
    };
}};
