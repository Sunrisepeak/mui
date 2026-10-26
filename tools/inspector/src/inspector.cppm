// openmui.inspector — the openmui Inspector (D-61, D-63, D-67).
//
// A tool written in openmui that connects to any running openmui application
// over its agent endpoint and shows what the runtime knows: the semantic tree
// (a virtualised list), the selected node's type, source line, properties,
// layout and state; the last frames' times as a graph against the 60 Hz
// budget; the frame's pipeline with each stage's plugin and time; and the
// application's clock, which it can pause, step, slow down and send back in
// time. Everything it shows, `omui agent` prints as JSON: the same data for
// people and for agents.
//
//     omui::inspector::Inspector inspector(runtime, {.app = "org.openmui.examples.notes"});
//     runtime.mount("main", inspector.view());
export module openmui.inspector;

export import :connection;
import std;
import openmui.core;
import openmui.view;
import openmui.agent;
import openmui.ui.base;
import openmui;

export namespace omui::inspector {

struct Options {
    std::string           app;          // the application to inspect; empty: the first other one that listens
    std::filesystem::path directory = agent::default_directory();
    seconds               poll{0.25};   // how often frames, clock and pipeline are read
    std::string           self = "org.openmui.inspector";  // never inspects itself
};

class Inspector {
public:
    Inspector(Runtime& ui, Options options);
    ~Inspector();
    Inspector(const Inspector&) = delete;
    Inspector& operator=(const Inspector&) = delete;

    [[nodiscard]] View view();

    // What the buttons do (and what tests and agents call).
    void choose(std::string app);
    void refresh();                      // the application list and the tree
    void select(std::string node);       // and read what the runtime knows of it
    void toggle_pause();
    void step(double seconds);           // while paused: one frame is 1/60
    void set_rate(double rate);
    void release_time();
    void set_overlay(std::string_view overlay, bool on);  // bounds, repaints, inspector, hud

    // What it shows.
    Signal<std::vector<std::string>> applications = signal(std::vector<std::string>{});
    Signal<std::string>              app = signal(std::string());
    Signal<std::string>              status = signal(std::string("looking for applications"));
    Signal<std::string>              selected = signal(std::string());
    Signal<std::string>              tab = signal(std::string("properties"));
    Signal<Value>                    details = signal(Value{});   // `inspect` of the selected node
    Signal<Value>                    stats = signal(Value{});     // `stats` with history
    Signal<Value>                    pipeline = signal(Value{});  // `pipeline`
    Signal<Value>                    clock = signal(Value{});     // `clock`
    Signal<std::size_t>              tree_size = signal(std::size_t{0});

    static constexpr std::string_view tree_model = "inspector.tree";
    // The tree model's row key for a node id, and back.
    [[nodiscard]] std::string key_of_node(std::string_view node) const;
    [[nodiscard]] std::string node_of_key(std::string_view key) const;

private:
    void poll();
    void read_tree();
    void read_details();
    void clock_action(Value request);

    Runtime&                 ui_;
    Options                  options_;
    Connection               connection_;
    std::uint64_t            timer_ = 0;
    std::int64_t             tree_revision_ = -1;
    int                      pending_ = 0;      // requests of the current poll round not yet answered
    std::map<std::string, std::string, std::less<>> ids_;  // tree row key → node id
};

// The program: the Inspector as an application (id org.openmui.inspector).
[[nodiscard]] AppSpec app(Options options);

}  // namespace omui::inspector
