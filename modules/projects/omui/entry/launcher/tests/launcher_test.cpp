// The shared runtime's launcher (E3b, D-250): what an application declares, the runtimes installed side by side, the
// newest that satisfies it chosen, and with none the reason for each and a command that installs one.
import std;
import mcpplibs.os;
import mcpplibs.mtest;
import omui.entry.launcher;

using namespace mcpplibs::mtest;
namespace launcher = omui::launcher;
namespace fs = std::filesystem;

namespace {

launcher::Runtime runtime(std::string version, std::string openmui, std::vector<std::string> capabilities) {
    return {.directory = fs::path("/runtimes") / version, .version = std::move(version), .openmui = std::move(openmui), .capabilities = std::move(capabilities)};
}

std::string described(std::string_view version, std::string_view openmui) {
    return std::format(R"({{"$format": "runtime@1", "runtime": {{"version": "{}", "openmui": "{}", "capabilities": ["agent", "gpu"]}}}})", version, openmui);
}

}  // namespace

UTest cases{"launcher", [] {
    "an application declares the specification, the capabilities it uses and, if it must, one runtime exactly"_test = [] {
        const auto wanted = launcher::declared(R"([package]
name = "hello"
[package.metadata.openmui.runtime]
openmui = "0.4"
needs = ["agent", "generate"]
runtime = "=0.1.0"
)");
        expect(fatal(wanted.has_value())) << (wanted ? "" : wanted.error());
        expect(eq(wanted->openmui, std::string("0.4")) && eq(wanted->needs.size(), std::size_t{2}) && eq(wanted->runtime, std::string("=0.1.0")));

        const auto none = launcher::declared("[package]\nname = \"hello\"\n");
        expect(!none && none.error().contains("[package.metadata.openmui.runtime]") && none.error().contains("openmui = \"0.4\"")) << "what to write";
        const auto loose = launcher::declared("[package.metadata.openmui.runtime]\nopenmui = \"0.4\"\nruntime = \"0.1.0\"\n");
        expect(!loose && loose.error().contains("runtime = \"=0.1.0\"")) << "a pin is exact, and the fix says so";
        const auto broken = launcher::declared("[package.metadata.openmui.runtime\n");
        expect(!broken && broken.error().starts_with("mcpp.toml:1:")) << broken.error();
    };

    "versions compare as numbers, part by part; a pre-release before its release"_test = [] {
        expect(launcher::compare_versions("0.1.10", "0.1.9") > 0);
        expect(launcher::compare_versions("0.2", "0.1.9") > 0);
        expect(launcher::compare_versions("0.1", "0.1.0") == 0);
        expect(launcher::compare_versions("0.1.0-dev.3", "0.1.0") < 0);
        expect(launcher::compare_versions("0.1.0-dev.10", "0.1.0-dev.9") > 0);
    };

    "the newest runtime that satisfies it starts it; a pin takes its own"_test = [] {
        const std::vector runtimes{runtime("0.2.0", "0.5", {"agent", "gpu"}), runtime("0.1.1", "0.4", {"agent", "gpu"}), runtime("0.1.0", "0.4", {"agent", "gpu", "generate"})};
        const auto newest = launcher::choose({.openmui = "0.4", .needs = {"agent"}}, runtimes);
        expect(fatal(newest.chosen.has_value())) << newest.why;
        expect(eq(newest.chosen->version, std::string("0.1.1"))) << "0.2.0 implements another specification";
        const auto needing = launcher::choose({.openmui = "0.4", .needs = {"generate"}}, runtimes);
        expect(needing.chosen && eq(needing.chosen->version, std::string("0.1.0"))) << "the newest with the capability";
        const auto pinned = launcher::choose({.openmui = "0.4", .runtime = "=0.1.0"}, runtimes);
        expect(pinned.chosen && eq(pinned.chosen->version, std::string("0.1.0")));
        const auto later = launcher::choose({.openmui = "0.5"}, runtimes);
        expect(later.chosen && eq(later.chosen->version, std::string("0.2.0"))) << "each application its specification's";
    };

    "with none it says why each installed one does not, and a command that installs one"_test = [] {
        const std::vector runtimes{runtime("0.1.0", "0.4", {"agent", "gpu"})};
        const auto missing = launcher::choose({.openmui = "0.4", .needs = {"generate"}}, runtimes);
        expect(!missing.chosen && missing.why.contains("0.1.0") && missing.why.contains("without generate")) << missing.why;
        expect(eq(missing.install, std::string("xlings install omui-runtime@0.1"))) << missing.install;
        const auto later_capability = launcher::choose({.openmui = "0.4", .needs = {"camera"}}, runtimes);
        expect(eq(later_capability.install, std::string("xlings install omui-runtime"))) << "a capability the released line lacks: the newest";
        const auto other = launcher::choose({.openmui = "0.5"}, runtimes);
        expect(!other.chosen && other.why.contains("openmui 0.4, not 0.5") && eq(other.install, std::string("xlings install omui-runtime"))) << other.why;
        const auto pinned = launcher::choose({.openmui = "0.4", .runtime = "=0.1.2"}, runtimes);
        expect(!pinned.chosen && eq(pinned.install, std::string("xlings install omui-runtime@0.1.2"))) << pinned.install;
        const auto nothing = launcher::choose({.openmui = "0.4"}, {});
        expect(!nothing.chosen && nothing.why.starts_with("no omui runtime is installed")) << nothing.why;
    };

    "the runtimes installed side by side, each its runtime.json; newest first"_test = [] {
        const fs::path home = mcpplibs::os::dirs::temp() / std::format("omui-launcher-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        const fs::path packages = home / "data" / "xpkgs" / "xim-x-omui-runtime";
        fs::create_directories(packages / "0.1.0");
        fs::create_directories(packages / "0.1.1" / "omui-runtime-0.1.1");  // unpacked into a directory of its own
        fs::create_directories(packages / "0.0.9");
        fs::create_directories(home / "data" / "xpkgs" / "local-x-omui-runtime" / "0.2.0-dev.1");  // a recipe registered beside the index
        expect(fatal(mcpplibs::os::fs::write(packages / "0.1.0" / "runtime.json", described("0.1.0", "0.4")).has_value()));
        expect(fatal(mcpplibs::os::fs::write(packages / "0.1.1" / "omui-runtime-0.1.1" / "runtime.json", described("0.1.1", "0.4")).has_value()));
        expect(fatal(mcpplibs::os::fs::write(packages / "0.0.9" / "runtime.json", R"({"$format": "runtime@1", "runtime": {}})").has_value()));
        expect(fatal(mcpplibs::os::fs::write(home / "data" / "xpkgs" / "local-x-omui-runtime" / "0.2.0-dev.1" / "runtime.json", described("0.2.0-dev.1", "0.4")).has_value()));
        const auto found = launcher::installed(home);
        expect(fatal(eq(found.size(), std::size_t{3}))) << "a description without its version is not a runtime";
        expect(eq(found[0].version, std::string("0.2.0-dev.1")) && eq(found[1].version, std::string("0.1.1")) && eq(found[2].version, std::string("0.1.0")));
        expect(eq(found[1].directory, packages / "0.1.1" / "omui-runtime-0.1.1") && eq(found[1].capabilities.size(), std::size_t{2}));
        expect(launcher::installed(home / "elsewhere").empty());

        const auto wrong = launcher::read_runtime(R"({"$format": "film@1"})", packages / "0.0.8");
        expect(!wrong && wrong.error().contains("runtime.json:1:") && wrong.error().contains("/$format")) << wrong.error();
        fs::remove_all(home);
    };
}};
