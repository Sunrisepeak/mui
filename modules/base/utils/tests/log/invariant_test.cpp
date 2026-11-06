// Only the native death probes use OS services, through mcpplibs.os.
// The production logger itself depends on the standard library alone.
import std;
import openmui.log;
import openmui.testing;
import mcpplibs.os;

using namespace omui::testing;
namespace os = mcpplibs::os;

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--invariant-failure") {
        omui::invariant(false, "independent invariant failure probe");
        return 0;  // must never be reached
    }
    if (argc > 1 && std::string_view(argv[1]) == "--fatal-failure")
        omui::fatal("independent fatal failure probe");
    Suite suite{"log.invariant"};
    suite.test("a true invariant returns without a diagnostic", [] {
        omui::set_log_history(0);
        omui::invariant(true, "satisfied");
        check(omui::recent_logs().empty());
    });
    if constexpr (!os::is_emscripten) {
        auto probe = [](std::string mode, std::string expected) {
            const auto image = os::env::image();
            check(image.has_value(), "the OS names this test executable");
            if (!image) return;
            os::process::spawn_options options;
            options.program = *image; options.args = {std::move(mode)};
            options.in = os::process::stdio::null;
            os::process::bounds limit;
            limit.hard = std::chrono::seconds(5);
            limit.grace = limit.drain = std::chrono::milliseconds(100);
            limit.out_limit = limit.err_limit = 64 * 1024;
            const auto child = os::process::run(std::move(options), limit);
            check(child.has_value(), "an independent process executes the failing invariant");
            if (!child) return;
            check(!child->timed_out && !child->held_open && !child->status.success(), "the invariant ends its own process");
            check(child->err.contains("openmui: invariant failed: " + expected), child->err);
            check(child->err.contains("invariant_test.cpp:") && child->err.contains("main"), "the fatal report preserves caller location");
        };
        suite.test("a false invariant reports its caller and terminates the subprocess", [&] {
            probe("--invariant-failure", "independent invariant failure probe");
        });
        suite.test("explicit fatal reports its caller and terminates the subprocess", [&] {
            probe("--fatal-failure", "independent fatal failure probe");
        });
    }
    return suite.run(argc, argv);
}
