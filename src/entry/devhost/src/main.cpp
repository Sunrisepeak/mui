// omui-devhost — `omui dev` for an application written in C++ (W16, D-154).
// The host owns the window; the project is built by mcpp in the background
// whenever its sources change; each build that succeeds runs in a process of
// its own, presenting to the host (--present-to), and takes over from the one
// before with what the person set, faded in; a build that fails leaves the one
// running and shows its problems over it. Agents read and drive it through
// the verbs dev.status and dev.rebuild.
import std;
import omui;
import mcpplibs.os;
import mcpplibs.cmdline;

namespace {

using namespace omui;
namespace os = mcpplibs::os;

// The project's sources: what a change of rebuilds (C++, its manifest, its build program).
std::map<std::filesystem::path, std::filesystem::file_time_type> stamps_of(const std::filesystem::path& project) {
    std::map<std::filesystem::path, std::filesystem::file_time_type> out;
    std::error_code error;
    for (auto it = std::filesystem::recursive_directory_iterator(project, std::filesystem::directory_options::skip_permission_denied, error);
         it != std::filesystem::recursive_directory_iterator(); it.increment(error)) {
        if (error) break;
        const std::string name = os::fs::utf8(it->path().filename());
        if (it->is_directory(error) && (name == "target" || name.starts_with("."))) {
            it.disable_recursion_pending();
            continue;
        }
        if (name.ends_with(".cpp") || name.ends_with(".cppm") || name == "mcpp.toml" || name == "build.mcpp")
            out.emplace(it->path(), std::filesystem::last_write_time(it->path(), error));
    }
    return out;
}

struct Builder {
    std::filesystem::path project;
    std::map<std::filesystem::path, std::filesystem::file_time_type> stamps;
    std::optional<os::process::process> building;
    std::optional<os::process::process> running;  // the build shown (the one starting, until it connects)
    std::string output;
    int builds = 0, failed = 0;
    std::string last;  // what the last build came to

    void collect() {
        if (!building) return;
        for (auto chunk = building->read_output_for(std::chrono::milliseconds(0)); chunk && *chunk && !(*chunk)->empty();
             chunk = building->read_output_for(std::chrono::milliseconds(0)))
            output += **chunk;
        for (auto chunk = building->read_error_for(std::chrono::milliseconds(0)); chunk && *chunk && !(*chunk)->empty();
             chunk = building->read_error_for(std::chrono::milliseconds(0)))
            output += **chunk;
    }

    bool start_build() {
        os::process::spawn_options options;
        options.program = "mcpp";
        options.args = {"build"};
        options.cwd = project;
        auto spawned = os::process::process::spawn(options);
        if (!spawned) {
            last = std::format("mcpp did not start: {}", spawned.error().message());
            return false;
        }
        building.emplace(std::move(*spawned));
        output.clear();
        ++builds;
        last = "building";
        return true;
    }
};

}  // namespace

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    auto command = command_line(App("omui-devhost")
                                    .description("omui dev for a C++ application: the host owns the window, builds come and go inside it (W16)")
                                    .option(Option("project").takes_value().value_name("DIR").help("the application's project (default: here)")),
                                argc, argv);
    if (!command) return command.error();
    const std::filesystem::path project = std::filesystem::absolute(os::fs::from_utf8(command->args.value("project").value_or(".")));
    auto builder = std::make_shared<Builder>();
    builder->project = project;
    std::unique_ptr<DevHost> dev;
    std::uint64_t watch = 0;
    AppSpec app;
    app.id = "org.openmui.devhost";
    app.title = std::format("omui dev — {}", os::fs::utf8(project.filename()));
    app.setup = [&](Runtime& rt) {
        dev = std::make_unique<DevHost>(rt, DevHostOptions{
            .directory = project / ".omui" / "dev",
            .start = [builder](const std::filesystem::path& socket) {
                os::process::spawn_options options;
                options.program = "mcpp";
                options.args = {"run", "-q", "--", "--present-to", os::fs::utf8(socket)};
                options.cwd = builder->project;
                options.out = options.err = os::process::stdio::inherit;
                options.bound_lifetime = true;
                auto spawned = os::process::process::spawn(options);
                if (!spawned) return false;
                builder->running.emplace(std::move(*spawned));
                return true;
            }});
        // The sources looked at a few times a second; a change (or the first look) builds.
        const auto tick = std::make_shared<std::function<void()>>();
        *tick = [&rt, &dev, &watch, builder, tick] {
            builder->collect();
            if (builder->building)
                if (auto ended = builder->building->try_wait(); ended && *ended) {
                    builder->collect();
                    const bool ok = (*ended)->shell_code() == 0;
                    builder->building.reset();
                    if (ok) {
                        builder->last = "built";
                        dev->show_problems({});
                        (void)dev->restart();
                    } else {
                        ++builder->failed;
                        auto problems = problems_in(builder->output);
                        if (problems.empty()) problems.push_back({.text = "the build failed (mcpp build's output said no more)"});
                        builder->last = std::format("failed: {} problem(s)", problems.size());
                        dev->show_problems(std::move(problems));
                    }
                }
            if (!builder->building) {
                auto now = stamps_of(builder->project);
                if (now != builder->stamps) {
                    builder->stamps = std::move(now);
                    (void)builder->start_build();
                }
            }
            watch = rt.after(seconds(0.25), *tick);
        };
        watch = rt.after(seconds(0.05), *tick);
        (void)rt.verbs().add({"dev.status", "What the development host shows: the builds, the application shown, problems", Effect::read, "", {}},
                             [&dev, builder](const Value&) -> VerbResult {
                                 return dev->status().set("builds", static_cast<std::int64_t>(builder->builds))
                                     .set("failed", static_cast<std::int64_t>(builder->failed))
                                     .set("building", builder->building.has_value())
                                     .set("last", builder->last)
                                     .set("project", os::fs::generic_utf8(builder->project));
                             });
        (void)rt.verbs().add({"dev.rebuild", "Builds the application again now, its sources changed or not", Effect::reversible, "", {}},
                             [builder](const Value&) -> VerbResult {
                                 if (builder->building) return std::unexpected(std::string("a build is running"));
                                 builder->stamps.clear();  // the next look builds
                                 return Value{}.set("rebuilding", true);
                             });
    };
    app.teardown = [&](Runtime& rt) {
        if (watch) rt.cancel(watch);
        dev.reset();
    };
    return run(std::move(app), command->options);
}
