// Only the native death probes use OS services, through mcpplibs.os.
// The production logger itself depends on the standard library alone.
import std;
import mlibs.log;
import mcpplibs.mtest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace os = mcpplibs::os;

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--invariant-failure") {
        mlibs::log::invariant(false, "independent invariant failure probe");
        return 0;  // must never be reached
    }
    if (argc > 1 && std::string_view(argv[1]) == "--fatal-failure")
        mlibs::log::fatal("independent fatal failure probe");
    "a true invariant returns without a diagnostic"_test = [] {
        mlibs::log::set_log_history(0);
        mlibs::log::invariant(true, "satisfied");
        expect(mlibs::log::recent_logs().empty());
    };
    if constexpr (!os::is_emscripten) {
        auto probe = [](std::string mode, std::string expected) {
            const auto image = os::env::image();
            expect(image.has_value()) << "the OS names this test executable";
            if (!image) return;
            os::process::spawn_options options;
            options.program = *image; options.args = {std::move(mode)};
            options.in = os::process::stdio::null;
            os::process::bounds limit;
            limit.hard = std::chrono::seconds(5);
            limit.grace = limit.drain = std::chrono::milliseconds(100);
            limit.out_limit = limit.err_limit = 64 * 1024;
            const auto child = os::process::run(std::move(options), limit);
            expect(child.has_value()) << "an independent process executes the failing invariant";
            if (!child) return;
            expect(!child->timed_out && !child->held_open && !child->status.success()) << "the invariant ends its own process";
            expect(child->err.contains("openmui: invariant failed: " + expected)) << (child->err);
            expect(child->err.contains("invariant_test.cpp:") && child->err.contains("main")) << "the fatal report preserves caller location";
        };
        "a false invariant reports its caller and terminates the subprocess"_test = [&] {
            probe("--invariant-failure", "independent invariant failure probe");
        };
        "explicit fatal reports its caller and terminates the subprocess"_test = [&] {
            probe("--fatal-failure", "independent fatal failure probe");
        };
    }
    return mcpplibs::mtest::run(argc, argv);
}
