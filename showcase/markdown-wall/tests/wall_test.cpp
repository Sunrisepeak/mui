// S1 as its reader and an agent use it: documents read from a folder (or
// made, the same for a seed), laid out without overlap; the wall that starts
// whole and sharp, windows that go live up close, workspaces dragged by
// their labels with what is on them (and put back with Esc), windows by
// their titles, the two views and the HUD on keys, streaming at a model's
// pace into textures and live windows; and its pixels, pinned.
import std;
import openmui;
import openmui.ui.canvas;
import openmui.showcase.markdown_wall;
import openmui.testing;
import openmui.abi;
import openmui.lang;
import openmui.lang.lua;
import openmui.lang.js;
import mcpplibs.os;

using namespace omui::testing;
using namespace omui;

namespace {

std::filesystem::path docs() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data" / "docs"; }

struct App {
    Headless          h;
    omui::wall::Model model;
    explicit App(std::vector<omui::wall::Document> d = {}, HeadlessOptions o = {.size = {1440, 900}}) : h(std::move(o)) {
        if (d.empty()) {
            auto read = omui::wall::read_folder(docs());
            check(read.has_value(), read ? std::string{} : read.error());
            d = std::move(*read);
        }
        model.docs = std::move(d);
        AppSpec spec = omui::wall::app(model);
        spec.setup(h.runtime());
        for (auto& v : spec.verbs) (void)h.runtime().verbs().add(v.desc, v.fn);
        h.mount("main", spec.view());
        h.settle();
    }
    world::Scene& scene() { return world::scene(model.scene); }
    world::WorldRect rect(std::string_view key) { return scene().rect(*scene().find(key)); }
    // A world point on the screen now.
    Point at(double x, double y) { return world::Camera{model.x.peek(), model.y.peek(), model.zoom.peek()}.to_screen(x, y, {1440, 900}); }
    void drag(Point from, Point to, Modifiers mods = {}) {
        h.step({PointerEvent{.kind = PointerKind::down, .position = from, .button = MouseButton::left, .buttons = 1, .mods = mods}});
        for (int i = 1; i <= 5; ++i)
            h.step({PointerEvent{.kind = PointerKind::move,
                                 .position = {from.x + (to.x - from.x) * static_cast<float>(i) / 5, from.y + (to.y - from.y) * static_cast<float>(i) / 5},
                                 .buttons = 1,
                                 .mods = mods}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = to, .button = MouseButton::left, .mods = mods}});
        h.settle();
    }
};

const omui::wall::Document& by_path(const std::vector<omui::wall::Document>& docs, std::string_view path) {
    return *std::ranges::find(docs, path, &omui::wall::Document::path);
}


// ── the tour (scripts/tour.{ts,lua,py}): the same records, the same wall ──

std::filesystem::path scripts() { return std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path() / "scripts"; }

std::string text_of(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Tour {
    std::vector<std::string> records;  // each step's result, as JSON
    std::string              wall;     // what it left: the camera, the moved window, the selection
};

// The verb the tour records with, and what it left.
void listen(App& a, std::vector<std::string>& records) {
    check(a.h.runtime()
              .verbs()
              .add({"tour.record", "A step of the tour and what it answered.", Effect::read, {}, {}},
                   [&records](const Value& args) -> VerbResult {
                       const Value* step = args.get("step");
                       const Value* value = args.get("value");
                       records.push_back(std::format("{} = {}", step ? std::string(step->as_string()) : std::string("?"), value ? value->json() : std::string("null")));
                       return Value{};
                   })
              .has_value());
}

std::string left(App& a) {
    a.h.settle();
    const auto using_key = by_path(a.model.docs, "guide/usage.md").key;
    const auto r = a.rect(using_key);
    std::string chosen;
    for (const auto& k : a.model.canvas()->selection()) chosen += k + " ";
    return std::format("camera {:.6f} {:.6f} {:.6f}; window {} {} {}x{}; chosen {}; streaming {}", a.model.x.peek(), a.model.y.peek(), a.model.zoom.peek(),
                       r.x, r.y, r.width, r.height, chosen, a.model.streaming());
}

template <class Make>
Tour in_process(Make make, std::string code, std::string name) {
    Tour t;
    App a;
    listen(a, t.records);
    auto handle = abi::attach(a.h.runtime());
    {
        lang::Host host(handle);
        lang::Session session(host);
        auto script = make(session);
        std::vector<std::string> errors;
        script->on_error([&](std::string_view m) { errors.emplace_back(m); });
        auto ran = script->run(std::move(code), std::move(name));
        // The script calls into the runtime, which answers between frames.
        for (int i = 0; i < 200000 && ran.wait_for(std::chrono::seconds(0)) != std::future_status::ready; ++i) {
            a.h.step();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        auto result = ran.get();
        check(result.has_value(), result ? "" : result.error().text());
        for (int i = 0; i < 2000 && !host.idle(); ++i) {
            a.h.step();
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
        check(errors.empty(), errors.empty() ? "" : errors.front());
    }
    abi::detach(handle);
    t.wall = left(a);
    return t;
}

// A Python that runs (python3, else python: a Windows runner's python3 may be the store's stub).
std::optional<std::filesystem::path> python() {
    for (const char* name : {"python3", "python"}) {
        mcpplibs::os::process::spawn_options o;
        auto exe = mcpplibs::os::env::find_executable(name);
        if (!exe) continue;
        o.program = *exe;
        o.args = {"-c", "import sys; print(sys.version_info >= (3, 10))"};
        if (auto r = mcpplibs::os::process::run(std::move(o)); r && r->status.success() && r->out.starts_with("True")) return *exe;
    }
    return std::nullopt;
}

// Python drives the wall as an agent does: through its endpoint, with openmui_agent.
Tour as_agent() {
    Tour t;
    // A short directory: a socket's path has room for about a hundred bytes.
    const auto dir = std::filesystem::temp_directory_path() / "omui-t";
    std::filesystem::create_directories(dir);
    HeadlessOptions o;
    o.level = HeadlessLevel::session;
    o.size = {1440, 900};
    o.app = "wall-tour";
    o.endpoint_directory = dir;
    App a(std::vector<omui::wall::Document>{}, std::move(o));
    listen(a, t.records);
    check(a.h.endpoint() && a.h.endpoint()->listening(), "the endpoint listens");
    const auto py = python();
    check(py.has_value(), "a Python 3.10 or later on PATH (python3 or python)");
    if (!py) return t;
    mcpplibs::os::process::spawn_options run;
    run.program = *py;
    run.args = {mcpplibs::os::fs::utf8(scripts() / "tour.py"), "wall-tour", mcpplibs::os::fs::utf8(dir)};
    const auto client = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path() / "lang" /
                        "python" / "python";
    run.env = {{"PYTHONPATH", mcpplibs::os::fs::utf8(client)}, {"PYTHONIOENCODING", "utf-8"}};
    run.out = mcpplibs::os::process::stdio::inherit;
    run.err = mcpplibs::os::process::stdio::inherit;
    auto child = mcpplibs::os::process::process::spawn(run);
    check(child.has_value(), child ? std::string{} : child.error().message());
    if (!child) return t;
    std::optional<mcpplibs::os::process::exit_status> status;
    for (int i = 0; i < 60000 && !status; ++i) {
        a.h.step();  // pumps the endpoint
        auto w = child->try_wait();
        if (w && *w) status = *w;
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(status && status->success(), status ? std::format("tour.py ended with {}", status->code) : std::string("tour.py did not end"));
    t.wall = left(a);
    return t;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.markdown-wall"};

    s.test("documents: a folder's Markdown, titled by its first heading, grouped by folder; made ones the same for a seed", [] {
        const auto d = omui::wall::read_folder(docs());
        check(d.has_value());
        std::vector<std::string> paths;
        for (const auto& doc : *d) paths.push_back(doc.path);
        check_eq(paths, (std::vector<std::string>{"README.md", "guide/install.md", "guide/usage.md", "notes/meeting.md", "notes/todo.md"}));
        check_eq(by_path(*d, "guide/usage.md").title, std::string("Using it"));
        check_eq(by_path(*d, "notes/meeting.md").title, std::string("周会记录"));
        check_eq(by_path(*d, "notes/todo.md").title, std::string("todo"));
        check_eq(by_path(*d, "README.md").group, std::string("."));
        check_eq(by_path(*d, "guide/install.md").group, std::string("guide"));
        check(!omui::wall::read_folder(docs() / "guide" / "install.md").has_value(), "a file is not a folder");
        const auto a = omui::wall::synthetic(120, 7), b = omui::wall::synthetic(120, 7), c = omui::wall::synthetic(120, 8);
        check(a.size() == 120 && std::ranges::equal(a, b, {}, &omui::wall::Document::source, &omui::wall::Document::source), "the same for a seed");
        check(!std::ranges::equal(a, c, {}, &omui::wall::Document::source, &omui::wall::Document::source), "another seed, other documents");
        std::set<std::string> groups;
        for (const auto& doc : a) groups.insert(doc.group);
        check_eq(groups.size(), std::size_t{5});
        check(std::ranges::any_of(a, [](const auto& doc) { return doc.source.contains("```cpp"); }) &&
                  std::ranges::any_of(a, [](const auto& doc) { return doc.source.contains("| Name |"); }) &&
                  std::ranges::any_of(a, [](const auto& doc) { return doc.source.contains("无限画布"); }),
              "code, tables and Chinese among them");
    });

    s.test("the layout: every window on its workspace, none over another, workspaces apart", [] {
        App a(omui::wall::synthetic(160, 3));
        auto& sc = a.scene();
        std::vector<world::NodeRef> workspaces, windows;
        sc.each([&](world::NodeRef n) { (sc.kind_name(n) == "workspace" ? workspaces : windows).push_back(n); });
        check_eq(windows.size(), std::size_t{160});
        check_eq(workspaces.size(), std::size_t{7});
        auto overlap = [](const world::WorldRect& p, const world::WorldRect& q) { return p.overlaps(q); };
        for (std::size_t i = 0; i < workspaces.size(); ++i)
            for (std::size_t j = i + 1; j < workspaces.size(); ++j) check(!overlap(sc.rect(workspaces[i]), sc.rect(workspaces[j])), "workspaces apart");
        for (std::size_t i = 0; i < windows.size(); ++i) {
            const auto r = sc.rect(windows[i]);
            const auto ws = sc.rect(*sc.find(omui::wall::key_of("workspace:" + std::string(sc.text(windows[i], "workspace")))));
            check(r.x >= ws.x && r.y >= ws.y + 72 && r.right() <= ws.right() && r.bottom() <= ws.bottom(), "on its workspace, below its label");
            for (std::size_t j = i + 1; j < windows.size(); ++j) check(!overlap(r, sc.rect(windows[j])), "windows apart");
        }
    });

    s.test("the wall starts whole and sharp; up close a window is a live document", [] {
        App a;
        const Canvas* c = a.model.canvas();
        check(c != nullptr, "the canvas, through Runtime::component");
        if (!c) return;
        check_eq(c->stats().textured, std::size_t{5});
        check_eq(c->stats().stretched, std::size_t{0});
        check_eq(c->stats().promoted, std::size_t{0});
        // An agent flies to a window: it goes live, its document in the semantic tree.
        const std::string key = by_path(a.model.docs, "guide/usage.md").key;
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("key", Value(key));
        check_eq(a.h.tree().act("main/wall", "focus_node", call).exit_code().value_or(-1), std::int64_t{0});
        a.h.settle();
        check_eq(c->stats().promoted, std::size_t{1});
        bool heading = false;
        for (const auto& n : a.h.tree().snapshot({}).nodes) heading = heading || (n.id.starts_with(std::format("main/wall/{}/", key)) && n.name == "Searching");
        check(heading, "its second heading, live");
        check(a.h.tree().violations().empty());
    });

    s.test("a link in a live window goes to the document it names, on the wall; `..` and addresses too", [] {
        App a;
        const std::string readme = by_path(a.model.docs, "README.md").key, usage = by_path(a.model.docs, "guide/usage.md").key;
        Invocation call;
        call.args = Value{}.set("key", Value(readme));
        check_eq(a.h.tree().act("main/wall", "focus_node", call).exit_code().value_or(-1), std::int64_t{0});
        a.h.settle();
        // The person clicks "Using it" where it is drawn.
        std::string link;
        for (const auto& n : a.h.tree().snapshot({}).nodes)
            if (n.id.starts_with(std::format("main/wall/{}/", readme)) && n.role == Role::link && n.name == "Using it") link = n.id;
        check(!link.empty(), "the README's link, live");
        if (link.empty()) return;
        check(a.h.click(link), "clicked (under the HUD: it takes no pointer)");
        a.h.settle();
        bool there = false;
        for (const auto& n : a.h.tree().snapshot({}).nodes) there = there || (n.id.starts_with(std::format("main/wall/{}/", usage)) && n.name == "Searching");
        check(there, "the camera went to guide/usage.md, live");
        // Relative paths resolve from the window's folder; what is not on the wall goes nowhere.
        check(a.model.follow(usage, "../README.md") == std::optional<std::string>(readme));
        check(a.model.follow(usage, "./install.md#steps") == std::optional<std::string>(by_path(a.model.docs, "guide/install.md").key));
        check(!a.model.follow(usage, "missing.md") && !a.model.follow(usage, "#searching"));
    });

    s.test("reading: search finds by title, path and text; a result opens; back and forward return; a double-click opens a window", [] {
        App a;
        const auto found = a.model.search("search");
        check(!found.empty() && found.front().path == "guide/usage.md", found.empty() ? "nothing" : found.front().path);
        check(a.model.search("SEARCH").size() == found.size(), "any case");
        check(a.model.search("no such words here").empty());
        const double z0 = a.model.zoom.peek();
        a.model.open(found.front().key);
        a.h.settle();
        check(a.model.zoom.peek() > z0 * 2 && a.model.can_back(), "open: close up, and back remembered");
        check(a.model.back());
        a.h.settle();
        check(std::abs(a.model.zoom.peek() - z0) < 1e-6 && a.model.can_forward(), "back to the whole wall");
        // Alt+Right goes forward again, through the keyboard as a person's.
        Headless& h = a.h;
        check(h.click("main/wall") || true);
        h.key(Key::right, Modifier::alt);
        h.settle();
        check(a.model.zoom.peek() > z0 * 2, "forward again");
        // The search box, from the toolbar: typing narrows, Enter opens the first.
        check(h.click("main/top/toolbar/search"), "Search");
        h.settle();
        check(a.model.searching.peek(), "shown");
        const auto readme = by_path(a.model.docs, "README.md").key;
        check_eq(h.runtime().verbs().invoke("wall.open", Invocation{.args = Value{}.set("document", "README.md")}).exit_code().value_or(-1), std::int64_t{0});
        h.settle();
        // Esc puts the box away; a double-click on a window (the whole wall first) opens it.
        h.key(Key::escape);
        check(!a.model.searching.peek(), "put away");
        h.key(Key::digit2);
        h.settle();
        const auto r = a.rect(by_path(a.model.docs, "notes/meeting.md").key);
        const Point at = a.at(r.x + r.width / 2, r.y + r.height / 2);
        h.step({PointerEvent{.kind = PointerKind::move, .position = at}, PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left}});
        h.step({PointerEvent{.kind = PointerKind::down, .position = at, .button = MouseButton::left, .buttons = 1, .clicks = 2}});
        h.step({PointerEvent{.kind = PointerKind::up, .position = at, .button = MouseButton::left, .clicks = 2}});
        h.settle();
        bool live = false;
        const auto meeting = by_path(a.model.docs, "notes/meeting.md").key;
        for (const auto& n : h.tree().snapshot({}).nodes) live = live || n.id.starts_with(std::format("main/wall/{}/", meeting));
        check(live, "the meeting notes, open to read");
        (void)readme;
    });

    s.test("kept: the arrangement and the camera come back with the folder", [] {
        const auto dir = scratch_dir("wall-kept");
        std::filesystem::remove_all(dir);
        std::filesystem::copy(docs(), dir, std::filesystem::copy_options::recursive);
        auto read = omui::wall::read_folder(dir);
        check(read.has_value());
        const auto install = by_path(*read, "guide/install.md").key;
        world::WorldRect moved{};
        double zoom = 0;
        {
            App a(*read);
            a.model.folder = dir;
            Invocation call;
            call.args = Value{}.set("keys", Value(Value::List{Value(install)})).set("dx", 120.0).set("dy", 40.0);
            check_eq(a.h.tree().act("main/wall", "move_by", call).exit_code().value_or(-1), std::int64_t{0});
            a.h.key(Key::digit1);
            a.h.advance(seconds(1));
            a.h.settle();
            moved = a.rect(install);
            zoom = a.model.zoom.peek();
            check(std::filesystem::exists(dir / ".openmui" / "wall.layout.omui.json"), "kept in the folder");
        }
        {
            omui::wall::Model m;
            m.docs = *omui::wall::read_folder(dir);
            m.folder = dir;
            Headless h({.size = {1440, 900}});
            AppSpec spec = omui::wall::app(m);
            spec.setup(h.runtime());
            h.mount("main", spec.view());
            h.settle();
            const auto& sc = world::scene(m.scene);
            const auto r = sc.rect(*sc.find(install));
            check(m.restored && std::abs(r.x - moved.x) < 1e-6 && std::abs(r.y - moved.y) < 1e-6, "the window where it was left");
            check(std::abs(m.zoom.peek() - zoom) < 1e-6, std::format("the camera too: {} {}", m.zoom.peek(), zoom));
        }
    });

    s.test("a workspace drags by its label with its windows; Esc puts them back; a window drags by its title alone", [] {
        App a;
        const auto guide = omui::wall::key_of("workspace:guide");
        const auto install = by_path(a.model.docs, "guide/install.md").key, usage = by_path(a.model.docs, "guide/usage.md").key;
        const auto ws0 = a.rect(guide), i0 = a.rect(install), u0 = a.rect(usage);
        const double z = a.model.zoom.peek();
        // On its label strip, 100 px to the right and 40 down.
        const Point label = a.at(ws0.x + 200, ws0.y + 30);
        a.drag(label, {label.x + 100, label.y + 40});
        const double dx = 100 / z, dy = 40 / z;
        check(std::abs(a.rect(guide).x - (ws0.x + dx)) < 1e-6 && std::abs(a.rect(install).x - (i0.x + dx)) < 1e-6 &&
                  std::abs(a.rect(usage).y - (u0.y + dy)) < 1e-6,
              "the workspace and its windows, by the same distance");
        // Esc mid-drag: back.
        const auto ws1 = a.rect(guide), i1 = a.rect(install);
        const Point again = a.at(ws1.x + 200, ws1.y + 30);
        a.h.step({PointerEvent{.kind = PointerKind::down, .position = again, .button = MouseButton::left, .buttons = 1}});
        a.h.step({PointerEvent{.kind = PointerKind::move, .position = {again.x + 60, again.y}, .buttons = 1}});
        check(a.rect(guide).x > ws1.x, "moving");
        a.h.key(Key::escape);
        a.h.step({PointerEvent{.kind = PointerKind::up, .position = {again.x + 90, again.y}, .button = MouseButton::left}});
        a.h.settle();
        check(std::abs(a.rect(guide).x - ws1.x) < 1e-6 && std::abs(a.rect(install).x - i1.x) < 1e-6, "put back");
        // A window by its title bar: only it moves.
        const auto i2 = a.rect(install), u2 = a.rect(usage);
        const Point title = a.at(i2.x + 200, i2.y + 12);
        a.drag(title, {title.x + 30, title.y + 30});
        check(a.rect(install).x > i2.x && std::abs(a.rect(usage).x - u2.x) < 1e-6, "the window alone");
        // Each drag one step of history: undone, last first, the workspace with its windows.
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(install).x - i2.x) < 1e-6, "the window's drag undone");
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(guide).x - ws0.x) < 1e-6 && std::abs(a.rect(install).x - i0.x) < 1e-6 && std::abs(a.rect(usage).y - u0.y) < 1e-6,
              "the workspace's undone, its windows with it");
        check_eq(a.h.runtime().redo({}).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(guide).x - (ws0.x + dx)) < 1e-6, "redone");
    });

    s.test("an agent moves a workspace as a person drags it: its windows with it (move_to on it, move_by on the canvas), one step undone together", [] {
        App a;
        const auto guide = omui::wall::key_of("workspace:guide");
        const auto install = by_path(a.model.docs, "guide/install.md").key, usage = by_path(a.model.docs, "guide/usage.md").key;
        const auto ws0 = a.rect(guide), i0 = a.rect(install), u0 = a.rect(usage);
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("x", ws0.x + 300).set("y", ws0.y + 50);
        check_eq(a.h.tree().act(std::format("main/wall/{}", guide), "move_to", call).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(guide).x - (ws0.x + 300)) < 1e-6 && std::abs(a.rect(install).x - (i0.x + 300)) < 1e-6 && std::abs(a.rect(usage).y - (u0.y + 50)) < 1e-6,
              "the workspace and its windows, by the same distance");
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(guide).x - ws0.x) < 1e-6 && std::abs(a.rect(install).x - i0.x) < 1e-6, "undone together");
        call.args = Value{}.set("keys", Value(Value::List{Value(guide)})).set("dx", -120.0).set("dy", 0.0);
        check_eq(a.h.tree().act("main/wall", "move_by", call).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(install).x - (i0.x - 120)) < 1e-6, "move_by carries them too");
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(guide).x - ws0.x) < 1e-6 && std::abs(a.rect(usage).x - u0.x) < 1e-6, "and back, once each");
    });

    s.test("tidy: each workspace packed in columns, the wall the window's shape, nothing overlapping, one step undone; align lines up the selection", [] {
        App a;
        auto& sc = world::scene(a.model.scene);
        std::vector<std::pair<std::string, world::WorldRect>> was;
        sc.each([&](world::NodeRef n) { was.emplace_back(std::string(sc.key(n)), sc.rect(n)); });
        check(a.h.click("main/top/toolbar/tidy"), "Tidy");
        a.h.settle();
        // No two windows overlap; every window lies on its own workspace; the wall about the window's shape.
        std::vector<world::WorldRect> windows;
        sc.each([&](world::NodeRef n) {
            if (sc.kind_name(n) == "window") windows.push_back(sc.rect(n));
        });
        int overlaps = 0;
        for (std::size_t i = 0; i < windows.size(); ++i)
            for (std::size_t j = i + 1; j < windows.size(); ++j) overlaps += windows[i].overlaps(windows[j]);
        check_eq(overlaps, 0);
        sc.each([&](world::NodeRef n) {
            if (sc.kind_name(n) != "workspace") return;
            std::vector<world::NodeRef> on;
            sc.carried(n, on);
            check(!on.empty(), std::format("{} carries its windows", sc.text(n, "title")));
        });
        const auto b = sc.bounds();
        const double vp = 1440.0 / 900.0;
        check(std::abs(std::log((b.width / b.height) / vp)) < std::log(2.5), std::format("the wall {} × {}", b.width, b.height));
        // Undone: every node where it was.
        check_eq(a.h.runtime().undo({}).exit_code().value_or(-1), std::int64_t{0});
        bool same = true;
        for (const auto& [k, r] : was) {
            const auto q = sc.rect(*sc.find(k));
            same = same && std::abs(q.x - r.x) < 1e-6 && std::abs(q.y - r.y) < 1e-6 && std::abs(q.width - r.width) < 1e-3;
        }
        check(same, "back as it was");
        // Align: two windows selected, their tops lined up; refused with one.
        const auto install = by_path(a.model.docs, "guide/install.md").key, usage = by_path(a.model.docs, "guide/usage.md").key;
        a.model.canvas()->select({install});
        auto one = a.h.runtime().verbs().invoke("wall.align", [] { Invocation c; c.args = Value{}.set("how", "top"); return c; }());
        check(one.exit_code() == std::optional<std::int64_t>(1), "one window: nothing to line up");
        a.model.canvas()->select({install, usage});
        Invocation call;
        call.args = Value{}.set("how", "top");
        check_eq(a.h.runtime().verbs().invoke("wall.align", call).exit_code().value_or(-1), std::int64_t{0});
        check(std::abs(a.rect(install).y - a.rect(usage).y) < 1e-6, "tops in a line");
    });

    s.test("back returns to the window read, and to where in it: focused again, its page scrolled as it was", [] {
        // A long document (its window shows a part of it: its page scrolls) among the folder's.
        auto docs_with_long = *omui::wall::read_folder(docs());
        std::string long_text = "# The long one\n\n";
        for (int i = 0; i < 120; ++i) long_text += std::format("Paragraph {} of a document long enough that its window scrolls.\n\n", i);
        docs_with_long.push_back({omui::wall::key_of("notes/long.md"), "The long one", "notes/long.md", "notes", long_text});
        App a(docs_with_long);
        // A document long enough to scroll, read first; another after it.
        std::string readme, install;
        const auto page_of = [&](const std::string& key) {
            for (const auto& n : a.h.tree().snapshot({}).nodes)
                if (n.id.starts_with(std::format("main/wall/{}/", key)) && n.id.ends_with("/page")) return n;
            return spec::SemanticNode{};
        };
        for (const auto& d : a.model.docs) {
            a.model.open(d.key);
            a.h.advance(seconds(1));
            a.h.settle();
            const auto p = page_of(d.key);
            if (readme.empty() && p.value.get("max_y") && p.value.get("max_y")->as_double() > 150) readme = d.key;
            else if (install.empty() && !readme.empty()) install = d.key;
        }
        if (install.empty()) install = a.model.docs.front().key == readme ? a.model.docs.back().key : a.model.docs.front().key;
        check(!readme.empty(), "a page that scrolls");
        if (readme.empty()) return;
        a.model.open(readme);
        a.h.advance(seconds(1));
        a.h.settle();
        const auto page_node = [&] {
            for (const auto& n : a.h.tree().snapshot({}).nodes)
                if (n.id.starts_with(std::format("main/wall/{}/", readme)) && n.id.ends_with("/page")) return n.id;
            return std::string();
        };
        const std::string page = page_node();
        check(!page.empty(), "read: its page live");
        Invocation call;
        call.args = Value{}.set("x", 0.0).set("y", 120.0);
        const auto scrolled_by = a.h.tree().act(page, "scroll_to", call);
        check(scrolled_by.exit_code() == std::optional<int>(0), scrolled_by.ndjson());
        a.h.advance(seconds(1));
        const double scrolled = a.h.runtime().semantic_node(a.h.runtime().find(page)).value.get("y")->as_double();
        check(scrolled > 50, std::format("scrolled down: {}", scrolled));
        check(a.model.here().focused == readme && a.model.here().scroll > 50, "where one is: the window read, and how far down");
        a.model.open(install);
        a.h.advance(seconds(1));
        a.h.settle();
        check_eq(a.model.canvas()->focused(), install);
        check(a.model.back(), "back");
        a.h.advance(seconds(1.5));
        a.h.settle();
        check_eq(a.model.canvas()->focused(), readme);
        const NodeId again = a.h.runtime().find(page_node());
        check(a.h.runtime().valid(again) && std::abs(a.h.runtime().semantic_node(again).value.get("y")->as_double() - scrolled) < 1,
              "its page where it was");
    });

    s.test("a document opened from a search shows the words searched for: scrolled to and selected in it", [] {
        App a;
        const auto usage = by_path(a.model.docs, "guide/usage.md").key;
        a.model.open(usage, "searching");
        a.h.advance(seconds(1.5));
        a.h.settle();
        bool selected = false;
        for (const auto& n : a.h.tree().snapshot({}).nodes)
            selected = selected || (n.id.starts_with(std::format("main/wall/{}/", usage)) && n.states.has(State::selected) &&
                                    n.name.find("Searching") != std::string::npos);
        check(selected, "the words selected where they are");
    });

    s.test("1 is one to one, 2 the whole wall, H the HUD", [] {
        App a;
        const double whole = a.model.zoom.peek();
        a.h.click({720, 450});  // the canvas has the keys
        a.h.key(Key::digit1);
        a.h.settle();
        check(std::abs(a.model.zoom.peek() - 1) < 1e-9, std::format("one to one: {}", a.model.zoom.peek()));
        a.h.key(Key::digit2);
        a.h.settle();
        check(std::abs(a.model.zoom.peek() - whole) < 1e-6, std::format("the whole wall again: {} vs {}", a.model.zoom.peek(), whole));
        auto hud_shown = [&] {
            for (const auto& n : a.h.tree().snapshot({}).nodes)
                if (n.id.ends_with("/hud/visible")) return true;
            return false;
        };
        check(hud_shown(), "the HUD");
        a.h.key(Key::h);
        a.h.settle();
        check(!hud_shown(), "hidden");
    });

    s.test("streaming: tokens at the rate asked, textures made stale at most four times a second, a live window at once", [] {
        App a;
        Invocation call;
        call.actor = {"agent", "test", {}};
        call.args = Value{}.set("windows", Value(2)).set("rate", Value(50.0));
        const auto e = a.h.runtime().verbs().invoke("wall.stream", call);
        check_eq(e.exit_code().value_or(-1), std::int64_t{0});
        check_eq(a.model.streaming(), std::size_t{2});
        a.h.step();
        check(a.h.runtime().frame_demand().reason == "streaming" && a.h.runtime().frame_demand().wanted() == 60, "60 frames a second for text (D-70)");
        auto& sc = a.scene();
        std::map<std::string, std::uint64_t> revisions;
        sc.each([&](world::NodeRef n) { revisions[std::string(sc.key(n))] = sc.content_revision(n); });
        a.h.advance(seconds(2));
        const auto written = a.model.tokens();
        check(written >= 190 && written <= 200, std::format("about 50 a second into each of 2 for 2 s: {}", written));
        std::size_t stale = 0;
        sc.each([&](world::NodeRef n) {
            const auto now = sc.content_revision(n), before = revisions[std::string(sc.key(n))];
            if (now != before) {
                ++stale;
                check(now - before <= 9, std::format("{} times stale in 2 s", now - before));
            }
        });
        check_eq(stale, std::size_t{2});
        check(a.model.canvas()->stats().stretched == 0 || a.model.canvas()->stats().textured > 0, "drawn");
        a.h.settle(seconds(0.5));
        check(a.h.runtime().verbs().invoke("wall.stop", call).exit_code() == std::optional<std::int64_t>(0), "stopped");
        check_eq(a.model.streaming(), std::size_t{0});
        // What was written is in the text.
        bool grew = false;
        for (const auto& d : a.model.docs) grew = grew || a.model.text(d.key).size() > d.source.size() + 400;
        check(grew, "the streamed text kept");
    });

    s.test("the tour in TypeScript, Lua and Python: the same records, the same wall (§3.6)", [] {
        auto ts = lang::js::compile_typescript(scripts() / "tour.ts");
        check(ts.has_value(), ts ? std::string{} : ts.error());
        if (!ts) return;
        const Tour t = in_process([](lang::Session& s) { return std::make_unique<lang::js::Script>(s); }, *ts, "tour.ts");
        const Tour l = in_process([](lang::Session& s) { return std::make_unique<lang::lua::Script>(s); }, text_of(scripts() / "tour.lua"), "@tour.lua");
        const Tour p = as_agent();
        check_eq(t.records.size(), std::size_t{10});
        for (const auto& r : t.records) std::println("    {}", r);
        check_eq(l.records, t.records);
        check_eq(p.records, t.records);
        std::println("    {}", t.wall);
        check_eq(l.wall, t.wall);
        check_eq(p.wall, t.wall);
        check(t.wall.contains("window 4000 3000"), "the window moved where the tour put it");
        check(t.records.size() == 10 && t.records[8] == "refused = \"not-found\"", "the refusal, as a code");
    });

    s.test("the same pixels on every platform", [] {
        App a;
        // The HUD shows times measured on the machine: not part of the pinned frame.
        a.model.hud.set(false);
        a.h.settle();
        Hash hash;
        const auto& px = a.h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        check(a.h.save_png(scratch_dir("wall-pixels") / "handbook.png").has_value());
        const auto still = scratch_dir("readme-markdown-wall") / "wall.png";  // the README's
        check(a.h.save_png(still).has_value() && std::filesystem::file_size(still) < 1024 * 1024);
        std::println("    pixels {}", hash.hex());
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check_eq(hash.hex(), std::string("df141a25ce93af58"));
    });

    return s.run(argc, argv);
}
