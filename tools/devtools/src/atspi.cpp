module omui.devtools.atspi;

import std;
import mcpplibs.os;
import mcpplibs.mpal;
import nlohmann.json;

namespace omui::devtools::atspi {
namespace {

namespace dbus = mcpplibs::mpal::dbus;
using Value = dbus::Value;

constexpr std::string_view accessible = "org.a11y.atspi.Accessible";
constexpr std::string_view text_interface = "org.a11y.atspi.Text";
constexpr std::string_view action_interface = "org.a11y.atspi.Action";

// A value's content, through a variant.
const Value& inner(const Value& value) {
    if (const auto* variant = std::get_if<dbus::Variant>(&value.data); variant && variant->second) return inner(*variant->second);
    return value;
}
std::optional<std::int64_t> integer(const Value& value) {
    return std::visit(
        [](const auto& held) -> std::optional<std::int64_t> {
            using T = std::remove_cvref_t<decltype(held)>;
            if constexpr (std::is_same_v<T, bool>) return held ? 1 : 0;
            else if constexpr (std::is_integral_v<T>) return static_cast<std::int64_t>(held);
            else return std::nullopt;
        },
        inner(value).data);
}
std::optional<double> real(const Value& value) {
    if (const auto* number = std::get_if<double>(&inner(value).data)) return *number;
    if (const auto whole = integer(value)) return static_cast<double>(*whole);
    return std::nullopt;
}
std::string text(const Value& value) {
    const auto* held = std::get_if<std::string>(&inner(value).data);
    return held ? *held : std::string{};
}
const std::vector<Value>& list(const Value& value) {
    static const std::vector<Value> none;
    const auto* held = std::get_if<std::vector<Value>>(&inner(value).data);
    return held ? *held : none;
}
Value of(std::int32_t number) { return {number}; }

struct Ref {
    std::string bus, path;
    friend bool operator==(const Ref&, const Ref&) = default;
};
struct Walked {
    Ref  ref;
    Node node;
};

// The accessibility bus, spoken to: a failed call answers nothing and is kept to say why.
class Tree {
public:
    explicit Tree(dbus::Bus bus) : bus_(std::move(bus)) {}

    std::vector<Value> call(const Ref& ref, std::string_view interface, std::string_view member, std::string_view signature = {}, std::vector<Value> arguments = {}) {
        auto answered = bus_.call({.destination = ref.bus, .path = ref.path, .interface = std::string(interface), .member = std::string(member),
                                   .signature = std::string(signature), .arguments = std::move(arguments)},
                                  std::chrono::milliseconds(10000));
        if (!answered) {
            failure = std::format("{}.{} on {}: {}", interface, member, ref.path, answered.error().text);
            return {};
        }
        return std::move(*answered);
    }
    Value get(const Ref& ref, std::string_view interface, std::string_view property) {
        auto body = call(ref, "org.freedesktop.DBus.Properties", "Get", "ss", {Value::of(std::string(interface)), Value::of(std::string(property))});
        return body.empty() ? Value{} : inner(body.front());
    }
    void set(const Ref& ref, std::string_view interface, std::string_view property, std::string signature, Value value) {
        (void)call(ref, "org.freedesktop.DBus.Properties", "Set", "ssv",
                   {Value::of(std::string(interface)), Value::of(std::string(property)), Value{dbus::Variant{std::move(signature), std::make_shared<Value>(std::move(value))}}});
    }
    std::string name(const Ref& ref) { return text(get(ref, accessible, "Name")); }
    std::string role(const Ref& ref) {
        const auto body = call(ref, accessible, "GetRole");
        return body.empty() ? std::string("unknown") : role_name(static_cast<std::uint32_t>(integer(body.front()).value_or(0)));
    }
    std::vector<Ref> children(const Ref& ref) {
        std::vector<Ref> out;
        const auto body = call(ref, accessible, "GetChildren");
        if (body.empty()) return out;
        for (const Value& child : list(body.front())) {
            const auto& pair = list(child);
            if (pair.size() == 2) out.push_back({text(pair[0]), text(pair[1])});
        }
        return out;
    }
    // Where a node is drawn, in the window (Component.GetExtents, ATSPI_COORD_TYPE_WINDOW): x, y, width, height.
    std::optional<std::array<std::int64_t, 4>> extents(const Ref& ref) {
        const auto body = call(ref, "org.a11y.atspi.Component", "GetExtents", "u", {Value{std::uint32_t{1}}});
        if (body.empty()) return std::nullopt;
        const auto& box = list(body.front());
        if (box.size() != 4) return std::nullopt;
        std::array<std::int64_t, 4> out{};
        for (std::size_t i = 0; i < 4; ++i) out[i] = integer(box[i]).value_or(0);
        return out;
    }
    void walk(const Ref& ref, std::int32_t depth, std::vector<Walked>& out) {
        out.push_back({ref, {name(ref), role(ref), depth}});
        if (depth < 30)
            for (const Ref& child : children(ref)) walk(child, depth + 1, out);
    }

    std::string failure;

private:
    dbus::Bus bus_;
};

// One example, running, and its window found in the registry.
class App {
public:
    App(Tree& tree, Report& report, const std::filesystem::path& binary, std::string window) : tree_(tree), report_(report), window_name_(std::move(window)) {
        mcpplibs::os::process::spawn_options spawn;
        spawn.program = binary;
        spawn.args = {"--cpu", "--no-agent"};
        spawn.env = {{"SDL_VIDEO_DRIVER", "offscreen"}, {"SDL_RENDER_DRIVER", "software"}};
        spawn.out = mcpplibs::os::process::stdio::null;
        spawn.err = mcpplibs::os::process::stdio::inherit;  // the example's own complaints, in the log beside the report
        auto started = mcpplibs::os::process::process::spawn(spawn);
        if (!started) {
            check(false, std::format("{}: started ({})", window_name_, started.error().message()));
            return;
        }
        process_ = std::move(*started);
        const Ref root{"org.a11y.atspi.Registry", "/org/a11y/atspi/accessible/root"};
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (std::chrono::steady_clock::now() < deadline && !window_) {
            // An application (named as its process), holding its window.
            for (const Ref& application : tree_.children(root))
                for (const Ref& ref : tree_.children(application))
                    if (tree_.name(ref) == window_name_) window_ = ref;
            if (!window_) std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        check(window_.has_value(), std::format("{}: in AT-SPI's registry, named as its window", window_name_));
        // Its tree as the application tells it once AT-SPI listens (the window may come first, alone, for a frame).
        for (int attempt = 0; window_ && attempt < 50 && nodes_.size() < 2; ++attempt) {
            if (attempt > 0) std::this_thread::sleep_for(std::chrono::milliseconds(200));
            nodes_.clear();
            tree_.walk(*window_, 0, nodes_);
        }
    }
    ~App() {
        if (!process_.valid()) return;
        process_.terminate();
        auto ended = process_.wait_for(std::chrono::seconds(10));
        if (!ended || !*ended) {
            process_.kill();
            (void)process_.wait();
        }
    }
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] bool found() const noexcept { return window_.has_value(); }
    bool check(bool ok, std::string what) {
        report_.checks.push_back({ok, std::move(what)});
        return ok;
    }
    const Walked* first(std::optional<std::string_view> name, std::optional<std::string_view> role = std::nullopt) const {
        for (const Walked& each : nodes_)
            if ((!name || each.node.name == *name) && (!role || each.node.role == *role)) return &each;
        return nullptr;
    }
    // The tree walked again until `holds` does, for at most `seconds`.
    bool until(const std::function<bool()>& holds, double seconds = 10) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
        while (std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            nodes_.clear();
            tree_.walk(*window_, 0, nodes_);
            if (holds()) return true;
        }
        return false;
    }
    bool enabled(const Walked& node) {
        const auto body = tree_.call(node.ref, accessible, "GetState");
        const auto& words = body.empty() ? list(Value{}) : list(body.front());
        return !words.empty() && ((integer(words.front()).value_or(0) >> 8) & 1) != 0;  // AT-SPI's STATE_ENABLED
    }
    bool press(const Walked& node) {
        const auto body = tree_.call(node.ref, action_interface, "DoAction", "i", {of(0)});
        return !body.empty() && integer(body.front()).value_or(0) != 0;
    }
    // Each node placed within its parent's place, as AT-SPI says them (each node's place composed through its
    // ancestors': a node's bounds are its own, its place in its parent its transform): the nodes outside, named.
    std::vector<std::string> outside() {
        std::vector<std::string> out;
        std::vector<std::pair<std::int32_t, std::optional<std::array<std::int64_t, 4>>>> above;  // depth, place
        for (const Walked& each : nodes_) {
            while (!above.empty() && above.back().first >= each.node.depth) above.pop_back();
            const auto box = tree_.extents(each.ref);
            const auto parent = std::ranges::find_if(above.rbegin(), above.rend(), [](const auto& a) { return a.second && (*a.second)[2] > 0; });
            if (box && (*box)[2] > 0 && (*box)[3] > 0 && parent != above.rend()) {
                const auto& [x, y, w, h] = *box;
                const auto& [px, py, pw, ph] = *parent->second;
                constexpr std::int64_t slack = 2;
                if (x < px - slack || y < py - slack || x + w > px + pw + slack || y + h > py + ph + slack)
                    out.push_back(std::format("{} ({}) at {},{} {}x{}, outside {},{} {}x{}", each.node.name, each.node.role, x, y, w, h, px, py, pw, ph));
            }
            above.emplace_back(each.node.depth, box);
        }
        return out;
    }
    [[nodiscard]] std::vector<Node> tree() const {
        std::vector<Node> out;
        for (const Walked& each : nodes_) out.push_back(each.node);
        return out;
    }

private:
    Tree&                                tree_;
    Report&                              report_;
    std::string                          window_name_;
    mcpplibs::os::process::process       process_;
    std::optional<Ref>                   window_;
    std::vector<Walked>                  nodes_;
};

void tally(App& app, Tree& tree, Report& report) {
    report.trees["Tally"] = app.tree();
    app.check(app.first(std::nullopt, "frame") || app.first(std::nullopt, "window"), "a window");
    const Walked* plus = app.first("+1");
    const Walked* minus = app.first("−1");
    app.check(plus && plus->node.role == "button", "the +1 button, spoken as a button");
    {
        const auto outside = app.outside();
        app.check(outside.empty(), std::format("every node placed within its parent in the window{}", outside.empty() ? std::string() : ": " + outside.front()));
        const auto a = plus ? tree.extents(plus->ref) : std::nullopt;
        const auto b = minus ? tree.extents(minus->ref) : std::nullopt;
        app.check(a && b && (*a)[2] > 0 && (*b)[2] > 0 && ((*a)[0] >= (*b)[0] + (*b)[2] || (*b)[0] >= (*a)[0] + (*a)[2] || (*a)[1] >= (*b)[1] + (*b)[3] || (*b)[1] >= (*a)[1] + (*a)[3]),
                  "+1 and −1 each at its own place in the window");
    }
    app.check(app.first("0", "label") || app.first("0", "static"), "the count, 0, as static text");
    app.check(app.first("Towards the goal", "progress bar"), "the progress, named");
    app.check(app.first("10 to go"), "the status");
    if (minus) app.check(!app.enabled(*minus), "−1 says it is disabled while the count is 0");
    if (plus) {
        const auto body = tree.call(plus->ref, action_interface, "GetActions");
        for (const Value& action : body.empty() ? list(Value{}) : list(body.front()))
            if (const auto& fields = list(action); !fields.empty()) report.actions.push_back(text(fields.front()));
        app.check(!report.actions.empty(), "the button offers an action");
        app.check(app.press(*plus), "pressed through the Action interface");
        app.check(app.until([&] { return app.first("1", "label") || app.first("1", "static"); }), "the count reads 1 afterwards");
        app.check(app.first("9 to go"), "and the status says what is left");
        minus = app.first("−1");
        app.check(minus && app.enabled(*minus), "−1 is enabled once there is something to take away");
    }
    // The goal slider through the Value interface: read, then set.
    if (const Walked* goal = app.first("Goal", "slider"); app.check(goal, "the goal, a slider")) {
        const Ref ref = goal->ref;  // the walk is redone below
        report.goal_value = real(tree.get(ref, "org.a11y.atspi.Value", "CurrentValue"));
        app.check(report.goal_value == 10.0, std::format("the goal reads 10 (read {})", report.goal_value.value_or(-1)));
        tree.set(ref, "org.a11y.atspi.Value", "CurrentValue", "d", Value{5.0});
        app.check(app.until([&] { return app.first("4 to go") != nullptr; }), "set to 5 through AT-SPI, the status says 4 to go");
    }
}

void notes(App& app, Tree& tree, Report& report) {
    report.trees["Notes"] = app.tree();
    const auto outside = app.outside();
    app.check(outside.empty(), std::format("every node placed within its parent in the window{}", outside.empty() ? std::string() : ": " + outside.front()));
    const Walked* draft = app.first("New note");
    if (!app.check(draft && (draft->node.role == "entry" || draft->node.role == "text"),
                   std::format("the draft field, an entry ({})", draft ? draft->node.role : "none")))
        return;
    const Ref ref = draft->ref;
    const auto read = [&] {
        const auto body = tree.call(ref, text_interface, "GetText", "ii", {of(0), of(-1)});
        return body.empty() ? std::string{} : text(body.front());
    };
    const auto number = [&](std::string_view property) { return integer(tree.get(ref, text_interface, property)).value_or(-1); };
    const auto yes = [](const std::vector<Value>& body) { return !body.empty() && integer(body.front()).value_or(0) != 0; };
    app.check(read().empty(), "its text read through Text: empty");
    app.check(yes(tree.call(ref, "org.a11y.atspi.EditableText", "SetTextContents", "s", {Value::of("buy oat milk")})), "written through EditableText");
    const bool read_back = app.until([&] { return read() == "buy oat milk"; });  // before the message reads it again
    app.check(read_back, std::format("and read back ({})", read()));
    app.check(number("CharacterCount") == 12, "twelve characters");
    // A screen reader's cursor: the caret moved, then a word selected.
    (void)tree.call(ref, "org.a11y.atspi.Component", "GrabFocus");
    app.check(app.until([&] { return number("CaretOffset") >= 0; }), "focused");
    app.check(yes(tree.call(ref, text_interface, "SetCaretOffset", "i", {of(4)})), "the caret set");
    const bool caret = app.until([&] { return number("CaretOffset") == 4; }, 5);
    app.check(caret, std::format("the caret reads 4 ({})", number("CaretOffset")));
    app.check(yes(tree.call(ref, text_interface, "SetSelection", "iii", {of(0), of(4), of(7)})), "a word selected");
    const auto selection = [&] {
        const auto body = tree.call(ref, text_interface, "GetSelection", "i", {of(0)});
        return body.size() == 2 ? std::pair{integer(body[0]).value_or(-1), integer(body[1]).value_or(-1)} : std::pair<std::int64_t, std::int64_t>{-1, -1};
    };
    const bool selected = app.until([&] { return selection() == std::pair<std::int64_t, std::int64_t>{4, 7}; }, 5);
    const auto now = selection();
    app.check(selected, std::format("the selection reads 4–7 ({}–{})", now.first, now.second));
    // The word at 5 (granularity 1: word).
    const auto word = tree.call(ref, text_interface, "GetStringAtOffset", "iu", {of(5), Value{std::uint32_t{1}}});
    report.word_at_5 = word.empty() ? std::string{} : text(word.front());
    const auto trimmed = [](std::string_view words) {
        while (!words.empty() && std::isspace(static_cast<unsigned char>(words.front()))) words.remove_prefix(1);
        while (!words.empty() && std::isspace(static_cast<unsigned char>(words.back()))) words.remove_suffix(1);
        return words;
    };
    app.check(trimmed(report.word_at_5) == "oat", std::format("the word at 5 is 'oat' ({})", report.word_at_5));
    if (const Walked* add = app.first("Add"); app.check(add, "the Add button")) {
        app.check(app.press(*add), "pressed");
        app.check(app.until([&] { return app.first("buy oat milk") != nullptr; }), "the note is in the list");
    }
}

}  // namespace

std::string role_name(std::uint32_t role) {
    static const std::map<std::uint32_t, std::string_view> names{
        {7, "check box"},     {16, "dialog"}, {23, "frame"},      {29, "label"},       {31, "list"},  {32, "list item"}, {39, "panel"},
        {42, "progress bar"}, {43, "button"}, {44, "radio button"}, {51, "slider"},  {61, "text"},  {62, "toggle button"}, {69, "window"},
        {73, "paragraph"},    {75, "application"}, {79, "entry"}, {83, "heading"},     {85, "section"}, {88, "link"},     {116, "static"},
        {129, "grouping"},
    };
    const auto found = names.find(role);
    return found != names.end() ? std::string(found->second) : std::format("role {}", role);
}

bool Report::ok() const noexcept {
    return !checks.empty() && std::ranges::all_of(checks, &Check::ok);
}

std::string Report::json() const {
    using json = nlohmann::json;
    json out{{"ok", ok()}, {"checks", json::array()}};
    for (const Check& each : checks) out["checks"].push_back({{"ok", each.ok}, {"what", each.what}});
    for (const auto& [window, nodes] : trees) {
        json listed = json::array();
        for (const Node& node : nodes) listed.push_back({{"name", node.name}, {"role", node.role}, {"depth", node.depth}});
        out[window == "Tally" ? "tally" : "notes"] = std::move(listed);
    }
    out["actions"] = actions;
    if (goal_value) out["goal_value"] = *goal_value;
    out["word_at_5"] = word_at_5;
    return out.dump(2);
}

Report check(const std::filesystem::path& tally_binary, const std::filesystem::path& notes_binary) {
    Report report;
    // The accessibility bus turned on, on the session bus, and its address asked for (as Orca does).
    auto session = dbus::Bus::session();
    if (!session) {
        report.checks.push_back({false, std::format("the session bus: {}", session.error().text)});
        return report;
    }
    const auto on = session->call({.destination = "org.a11y.Bus", .path = "/org/a11y/bus", .interface = "org.freedesktop.DBus.Properties", .member = "Set", .signature = "ssv",
                                   .arguments = {Value::of("org.a11y.Status"), Value::of("IsEnabled"), Value{dbus::Variant{"b", std::make_shared<Value>(Value{true})}}}},
                                  std::chrono::milliseconds(10000));
    const auto address = session->call({.destination = "org.a11y.Bus", .path = "/org/a11y/bus", .interface = "org.a11y.Bus", .member = "GetAddress"},
                                       std::chrono::milliseconds(10000));
    if (!on || !address || address->empty()) {
        report.checks.push_back({false, std::format("the accessibility bus turned on: {}", !on ? on.error().text : !address ? address.error().text : "no address")});
        return report;
    }
    auto bus = dbus::Bus::address(text(address->front()));
    if (!bus) {
        report.checks.push_back({false, std::format("the accessibility bus at {}: {}", text(address->front()), bus.error().text)});
        return report;
    }
    Tree tree(std::move(*bus));
    const auto one = [&](const std::filesystem::path& binary, std::string window, void (*run)(App&, Tree&, Report&)) {
        tree.failure.clear();
        App app(tree, report, binary, window);
        if (app.found()) run(app, tree, report);
        if (!tree.failure.empty()) report.checks.push_back({false, std::format("{}: {}", window, tree.failure)});
    };
    one(tally_binary, "Tally", tally);
    if (!notes_binary.empty()) one(notes_binary, "Notes", notes);
    return report;
}

}  // namespace omui::devtools::atspi
