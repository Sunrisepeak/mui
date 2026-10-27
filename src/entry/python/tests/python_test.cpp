// The Python binding, tested from Python: the extension this package builds
// is staged with the `openmui` package (lang/python/python) as an importable
// package, and the binding's own unittest suite (lang/python/tests) runs in
// the system's CPython 3.10+ against it, headless.
import std;
import mcpplibs.os;
import openmui.testing;

using namespace omui::testing;
namespace os = mcpplibs::os;

namespace {

std::filesystem::path repo() {
    return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path().parent_path();
}

// The extension mcpp built: a shared library with `_openmui` in its name,
// near this program.
std::optional<std::filesystem::path> extension(const std::filesystem::path& self) {
    for (auto dir = self.parent_path(); !dir.empty() && dir != dir.parent_path(); dir = dir.parent_path()) {
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(dir, ec); !ec && it != std::filesystem::recursive_directory_iterator();
             it.increment(ec)) {
            const std::string name = os::fs::utf8(it->path().filename());
            if (name.find("_openmui") != std::string::npos && it->path().extension() == os::library::suffix()) return it->path();
        }
        if (dir.filename() == "target") break;
    }
    return std::nullopt;
}

// libX.so, libX.so.6, X.dll, libX.dylib.
bool is_shared_library(std::string_view name) {
    const std::string_view suffix = os::library::suffix();
    return name.ends_with(suffix) || (!os::is_windows && name.find(std::string(suffix) + ".") != std::string_view::npos);
}

std::optional<std::filesystem::path> python() {
    for (const char* name : {"python3", "python"})
        if (auto p = os::env::find_executable(name)) return p;
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"entry.python"};
    const std::filesystem::path self = std::filesystem::absolute(os::fs::from_utf8(argc > 0 ? argv[0] : ""));

    s.test("the binding's suite passes in CPython against the built extension", [&] {
        auto ext = extension(self);
        check(ext.has_value(), "the _openmui extension is built beside the test");
        auto py = python();
        check(py.has_value(), "a CPython 3.10+ is on PATH");
        if (!ext || !py) return;
        // An importable package: openmui/__init__.py and openmui/_openmui<suffix>.
        const auto stage = scratch_dir("entry-python");
        std::filesystem::create_directories(stage / "openmui");
        std::filesystem::copy(repo() / "lang" / "python" / "python" / "openmui", stage / "openmui",
                              std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing);
        const std::string module_name = os::is_windows ? "_openmui.pyd" : "_openmui.abi3.so";
        std::filesystem::copy_file(*ext, stage / "openmui" / module_name, std::filesystem::copy_options::overwrite_existing);
        // The shared libraries mcpp put beside it (SDL3's X11 stack…): the
        // extension finds them in its own directory, as in a wheel.
        for (const auto& dep : std::filesystem::directory_iterator(ext->parent_path())) {
            const std::string name = os::fs::utf8(dep.path().filename());
            if (!dep.is_regular_file() || dep.path() == *ext || !is_shared_library(name)) continue;
            std::filesystem::copy_file(dep.path(), stage / "openmui" / dep.path().filename(), std::filesystem::copy_options::overwrite_existing);
        }
        os::process::spawn_options o;
        o.program = *py;
        o.args = {"-m", "unittest", "discover", "-s", os::fs::utf8(repo() / "lang" / "python" / "tests"), "-v"};
        o.env = {{"PYTHONPATH", os::fs::utf8(stage)}, {"OMUI_HEADLESS", "1"}, {"PYTHONUTF8", "1"}};
        // Under AddressSanitizer (the nightly's sanitize profile) the
        // extension is instrumented and the interpreter is not: the sanitizer's
        // shared runtime is put in the process first, and the nightly names it.
        // Leaks are not asked about: the interpreter keeps what it made at exit,
        // and so does the extension's world (src/extension.cpp).
        if (os::library::process_symbol("__asan_init")) {
            auto runtime = os::env::get("OMUI_ASAN_RUNTIME");
            check(runtime && std::filesystem::exists(os::fs::from_utf8(*runtime)),
                  "under ASan, OMUI_ASAN_RUNTIME names the sanitizer's shared runtime "
                  "(clang's libclang_rt.asan.so)");
            if (!runtime) return;
            o.env.emplace_back("LD_PRELOAD", *runtime);
            o.env.emplace_back("ASAN_OPTIONS", "detect_leaks=0:verify_asan_link_order=0:abort_on_error=1");
        }
        auto r = os::process::run(std::move(o));
        check(r.has_value(), r ? "" : r.error().message());
        if (!r) return;
        std::print("{}", r->err);
        check(r->status.success(), std::format("python exited with {}", r->status.code));
    });

    return s.run(argc, argv);
}
