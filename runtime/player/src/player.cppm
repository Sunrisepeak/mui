// openmui.player — an application's views, components and scripts, run and
// reloaded in place (component-binding §9, §10.2).
//
//     omui::player::Player p(runtime, manifest, {omui::lang::lua::language()});
//     if (auto r = p.start(); !r) …;
//     p.watch();   // saved files show at once
//
// Views and components are view documents (openmui.viewdoc); a script runs in
// a Language, a plugin the entry registers (the player knows none itself), on
// a thread of its own, over the C ABI. A file that changes is reloaded in
// place: a view keeps its nodes and state, a component its instances, a
// script its signals. A file with an error changes nothing; the error is kept
// in problems() and logged.
export module openmui.player;

import std;
import openmui.core;
import openmui.view;
import openmui.abi;

export namespace omui::player {

struct Script {
    std::string           lang;   // a registered Language's name: lua, js, ts …
    std::filesystem::path entry;
};

// [package.metadata.openmui] of an application (the entry reads it).
struct Manifest {
    std::string                        id = "openmui-app";
    std::string                        title = "openmui";
    Size                               size{960, 640};
    std::filesystem::path              base;        // the paths below are relative to it
    std::vector<std::filesystem::path> components;  // component documents, defined first
    std::vector<std::filesystem::path> views;       // view documents, mounted at their files' stems
    std::vector<Script>                scripts;
};

using Status = std::expected<void, std::string>;

// One script's machine (a Lua state, a JavaScript context …).
class Engine {
public:
    virtual ~Engine() = default;
    [[nodiscard]] virtual std::future<Status> run(std::string code, std::string name) = 0;
    // Runs the new text in place: the script's signals keep their values.
    [[nodiscard]] virtual std::future<Status> reload(std::string code, std::string name) = 0;
    // Nothing left to do on its thread, and nothing in flight to the runtime.
    [[nodiscard]] virtual bool idle() const = 0;
};

struct Language {
    std::string              name;        // as manifests write it
    std::vector<std::string> extensions;  // .lua, .js, .ts …
    std::function<std::unique_ptr<Engine>(abi::OmuiRuntime)> make;
    // The source of an entry: the file, or what a compiler makes of it
    // (TypeScript through esbuild). Empty: the file as it is.
    std::function<std::expected<std::string, std::string>(const std::filesystem::path&)> load;
};

class Player {
public:
    Player(Runtime& runtime, Manifest manifest, std::vector<Language> languages);
    ~Player();
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // Components, then views, then scripts. Errors are reported together.
    Status start();
    // Reloads what changed on disk since the last look; answers what it
    // reloaded (its paths). Errors go to problems().
    std::vector<std::string> poll();
    // Polls on the runtime's timers, every `period`.
    void watch(seconds period = seconds(0.1));

    // The scripts are done for now (tests and `--headless` wait for it).
    [[nodiscard]] bool idle() const;
    [[nodiscard]] std::span<const std::string> problems() const noexcept;
    [[nodiscard]] const Manifest& manifest() const noexcept;
    [[nodiscard]] abi::OmuiRuntime handle() const noexcept;

    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::player
