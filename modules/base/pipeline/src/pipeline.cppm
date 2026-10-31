// openmui.pipeline — a frame as a graph of stages (D-66).
//
// A stage is a plugin that reads and writes named, typed resources (an event
// list, a node tree, a display list, a surface …). A pipeline is a list of
// stages built against a registry of plugins: the build checks that every
// resource a stage reads is written by a stage before it (or provided from
// outside), that the types agree, that each resource has one writer, and
// that the order constraints have no cycle — and reports each problem with
// where it is. Running a pipeline runs its stages in a deterministic order
// (dependencies first, then the order they were declared), times each one,
// and traces each one as a span; a stage's plugin can be replaced between
// runs, and a replacement that does not build leaves the old one in place.
//
// Stages are called once per run, never per node or per pixel (D-65): the
// virtual call is the boundary between plugins, not the hot path.
//
//     omui::pipeline::Registry plugins;
//     plugins.add({.name = "acme.blur", .kind = "stage"}, [](const Config&) { return std::make_unique<Blur>(); });
//     auto p = omui::pipeline::Pipeline::build(app_pipeline(), plugins, inputs);  // app_pipeline(): generated from app.pipeline.omui.toml
//     p->run(resources);
//
// An independent component (D-55): the standard library and openmui.trace.
// `<name>.pipeline.omui.toml` is read at the edges: omui-gen (and the
// a build plugin under mcpp/) turns it into a C++ Description at build time,
// and tools send changes as JSON over the agent protocol; this package parses
// nothing, so it throws nothing.
export module openmui.pipeline;

import std;

export namespace omui::pipeline {

// ── resources ─────────────────────────────────────────────────────────────

// A type that stages exchange names itself:
//     template <> struct omui::pipeline::resource_type<DisplayList> { static constexpr std::string_view name = "render.DisplayList"; };
template <class T>
struct resource_type;

template <class T>
concept Resource = requires {
    { resource_type<T>::name } -> std::convertible_to<std::string_view>;
};

// A port: a resource a stage reads or writes, by name and type name.
struct Port {
    std::string name;
    std::string type;
    friend bool operator==(const Port& a, const Port& b) noexcept { return a.name == b.name && a.type == b.type; }
};

template <Resource T>
[[nodiscard]] Port port(std::string name) {
    return {std::move(name), std::string(resource_type<T>::name)};
}

// The resources of one run: borrowed pointers by name, checked against their
// type name when read. Lookups compare a handful of names; nothing allocates
// once the slots exist.
class Resources {
public:
    template <Resource T>
    void put(std::string_view name, T* value) {
        set(name, resource_type<T>::name, value);
    }
    template <Resource T>
    [[nodiscard]] T* get(std::string_view name) const noexcept {
        return static_cast<T*>(find(name, resource_type<T>::name));
    }
    [[nodiscard]] bool has(std::string_view name) const noexcept;
    void clear() noexcept { slots_.clear(); }

private:
    struct Slot {
        std::string      name;
        std::string_view type;
        void*            value = nullptr;
    };
    void set(std::string_view name, std::string_view type, void* value);
    [[nodiscard]] void* find(std::string_view name, std::string_view type) const noexcept;
    std::vector<Slot> slots_;
};

// ── configuration ─────────────────────────────────────────────────────────

using ConfigValue = std::variant<bool, std::int64_t, double, std::string>;

// A stage's configuration, from its `config = { … }` table.
class Config {
public:
    Config() = default;
    Config(std::initializer_list<std::pair<std::string, ConfigValue>> values) : values_(values) {}
    void set(std::string key, ConfigValue value);
    [[nodiscard]] const ConfigValue* get(std::string_view key) const noexcept;
    [[nodiscard]] bool flag(std::string_view key, bool fallback = false) const noexcept;
    [[nodiscard]] std::int64_t integer(std::string_view key, std::int64_t fallback = 0) const noexcept;
    [[nodiscard]] double number(std::string_view key, double fallback = 0) const noexcept;
    [[nodiscard]] std::string_view text(std::string_view key, std::string_view fallback = {}) const noexcept;
    [[nodiscard]] std::span<const std::pair<std::string, ConfigValue>> entries() const noexcept { return values_; }
    friend bool operator==(const Config&, const Config&) = default;

private:
    std::vector<std::pair<std::string, ConfigValue>> values_;
};

// ── stages and plugins ────────────────────────────────────────────────────

struct Context {
    Resources&    resources;
    std::uint64_t run = 0;  // how many runs before this one
};

class Stage {
public:
    virtual ~Stage() = default;
    // What it reads and writes. A resource it both reads and writes (updates)
    // is listed in both.
    [[nodiscard]] virtual std::vector<Port> reads() const = 0;
    [[nodiscard]] virtual std::vector<Port> writes() const = 0;
    virtual std::expected<void, std::string> run(Context& context) = 0;
};

struct PluginInfo {
    std::string name;                // "openmui.render.cpu"
    std::string kind = "stage";      // the plugin kinds of D-65
    std::string interface;           // "omui.pipeline.stage@1"
    std::string description;
    std::string trace_category = "frame";
};

using Factory = std::function<std::expected<std::unique_ptr<Stage>, std::string>(const Config&)>;

class Registry {
public:
    // A second plugin of the same name replaces the first.
    void add(PluginInfo info, Factory make);
    [[nodiscard]] const PluginInfo* info(std::string_view name) const noexcept;
    [[nodiscard]] std::expected<std::unique_ptr<Stage>, std::string> make(std::string_view name, const Config& config) const;
    [[nodiscard]] std::vector<PluginInfo> plugins() const;

private:
    struct Entry {
        PluginInfo info;
        Factory    make;
    };
    std::vector<Entry> entries_;
};

// ── descriptions ──────────────────────────────────────────────────────────

struct StageDesc {
    std::string              name;    // unique in its pipeline
    std::string              plugin;
    std::vector<std::string> after;   // stages it runs after, beyond what its data needs
    Config                   config;
    friend bool operator==(const StageDesc&, const StageDesc&) = default;
};

struct Description {
    std::string            name;
    std::vector<StageDesc> stages;
    friend bool operator==(const Description&, const Description&) = default;
};

struct Problem {
    std::string where;    // "app.pipeline.omui.toml:12" or "stage `raster`"
    std::string message;
    std::string hint;
};

[[nodiscard]] std::string to_text(std::span<const Problem> problems);

// A description as `<name>.pipeline.omui.toml`:
//     name = "desktop"
//     [[stage]]
//     name = "raster"
//     plugin = "openmui.render.cpu"
//     after = ["damage"]
//     config = { parallel = true }
[[nodiscard]] std::string to_toml(const Description& description);

// ── pipelines ─────────────────────────────────────────────────────────────

struct StageTime {
    std::string name;
    double      ms = 0;
};

class Pipeline {
public:
    Pipeline(Pipeline&&) noexcept;
    Pipeline& operator=(Pipeline&&) noexcept;
    ~Pipeline();

    // `inputs`: what the caller provides to every run (FrameInput, a target surface …).
    [[nodiscard]] static std::expected<Pipeline, std::vector<Problem>> build(const Description& description, const Registry& registry,
                                                                            std::span<const Port> inputs = {});

    // Runs every stage once, in order; stops at the first stage that fails and
    // says which.
    std::expected<void, std::string> run(Resources& resources);

    // The last run's time per stage, in order.
    [[nodiscard]] std::span<const StageTime> timings() const noexcept;
    // The order the stages run in.
    [[nodiscard]] std::vector<std::string> order() const;
    // Whether a stage writes this resource (what a run will have).
    [[nodiscard]] bool produces(std::string_view resource) const noexcept;
    [[nodiscard]] const Description& description() const noexcept;
    // Every resource with its writer and readers, the order, and the plugins:
    // what tools draw (`omui agent pipeline`, the pipeline workshop).
    [[nodiscard]] std::string describe() const;

    // Rebuilds with one stage's plugin (or configuration) changed. On a
    // problem the pipeline is unchanged and the problems are returned.
    std::expected<void, std::vector<Problem>> replace(std::string_view stage, std::string_view plugin, const Config& config,
                                                      const Registry& registry);
    // Rebuilds with a stage added — declared right after the last stage its
    // `after` names, else last — or removed.
    std::expected<void, std::vector<Problem>> insert(StageDesc stage, const Registry& registry);
    std::expected<void, std::vector<Problem>> remove(std::string_view stage, const Registry& registry);

private:
    struct Impl;
    explicit Pipeline(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace omui::pipeline
