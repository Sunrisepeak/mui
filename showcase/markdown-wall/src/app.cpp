// The wall's model (live text, streaming), its HUD and the application.
module openmui.showcase.markdown_wall;

import std;
import openmui.hash;
import openmui;
import openmui.ui.canvas;
import openmui.ui.canvas.kit;
import openmui.ui.markdown;
import mcpplibs.os;

namespace omui::wall {

namespace {

constexpr std::array<std::string_view, 24> tokens{"the",  "wall", "streams", "text", "as",    "a",     "model", "writes", "it",   "token", "by",   "token",
                                                  "and",  "only", "the",     "tail", "is",    "parsed", "again", "while",  "every", "frame", "stays", "smooth"};

// A stream's n-th token: words, sentences, now and then a new paragraph.
std::string token(std::string_view key, std::uint64_t n) {
    const std::uint64_t h = omui::hash::fnv1a(key);
    std::string t(tokens[(h + n * 7) % tokens.size()]);
    if (n % 60 == 0) return "\n\n" + std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(t[0])))) + t.substr(1);
    if (n % 12 == 11) return " " + t + ".";
    return " " + t;
}

}  // namespace

Model::~Model() {
    // Nothing a kind or a timer of this model holds outlives it.
    alive_.reset();
    omui::world::drop_scene(scene);
}

const Document* Model::document(std::string_view key) const {
    for (const Document& d : docs)
        if (d.key == key) return &d;
    return nullptr;
}

omui::Signal<std::string> Model::live(std::string_view key) {
    auto it = live_.find(std::string(key));
    if (it == live_.end()) {
        const Document* d = document(key);
        it = live_.emplace(std::string(key), omui::signal(d ? d->source : std::string{})).first;
    }
    return it->second;
}

std::string Model::text(std::string_view key) const {
    if (auto it = live_.find(std::string(key)); it != live_.end()) return it->second.peek();
    const Document* d = document(key);
    return d ? d->source : std::string{};
}

void Model::open(std::string_view key, std::string_view words) {
    if (!runtime || !document(key)) return;
    if (!words.empty()) show_words(std::string(key), std::string(words), 16);
    back_.push_back(here());
    if (back_.size() > 100) back_.erase(back_.begin());
    forward_.clear();
    if (const NodeId c = runtime->find(canvas_path); runtime->valid(c)) {
        Invocation call;
        call.args = Value{}.set("key", std::string(key));
        (void)runtime->act(c, "focus_node", call);
    }
}

void Model::open_search() {
    searching.set(true);
    if (runtime)  // once it is laid out: typing goes into it
        (void)runtime->after(seconds(0), [this, alive = std::weak_ptr<int>(alive_)] {
            if (alive.expired() || !runtime) return;
            if (const NodeId q = runtime->find(query_path); runtime->valid(q)) (void)runtime->act(q, "focus");
        });
}

void Model::close_search() {
    searching.set(false);
    if (runtime)
        if (const NodeId c = runtime->find(canvas_path); runtime->valid(c)) (void)runtime->act(c, "focus");
}

namespace {

// A live window's page (its scroll), wherever the canvas put the window's element: none while it is not live.
NodeId page_of(const omui::Runtime& rt, std::string_view canvas_path, std::string_view key) {
    // From the canvas (the window's own path is its item's, which the runtime does not hold).
    const std::string within = std::format("{}/{}/", canvas_path, key);
    std::vector<NodeId> todo{rt.find(canvas_path)};
    while (!todo.empty()) {
        const NodeId n = todo.back();
        todo.pop_back();
        if (!rt.valid(n)) continue;
        if (const std::string p = rt.path(n); p.starts_with(within) && p.ends_with("/page")) return n;
        for (const NodeId c : rt.children(n)) todo.push_back(c);
    }
    return {};
}

}  // namespace

Model::Camera Model::here() const {
    Camera c{x.peek(), y.peek(), zoom.peek(), {}, 0};
    if (const Canvas* cv = canvas(); cv && runtime) {
        c.focused = cv->focused();
        // How far down its page the window read was (its live page's scroll).
        if (!c.focused.empty())
            if (const NodeId page = page_of(*runtime, canvas_path, c.focused); runtime->valid(page)) {
                const spec::SemanticNode seen = runtime->semantic_node(page);  // kept: what is read from it points into it
                if (const Value* down = seen.value.get("y")) c.scroll = down->as_double();
            }
    }
    return c;
}

void Model::go(const Camera& c) {
    Canvas* cv = canvas();
    if (!cv) {
        x.set(c.x), y.set(c.y), zoom.set(c.zoom);
        return;
    }
    // The window read then, read again — live and focused — and its page where it was, once it is live.
    cv->focus_only(c.focused);
    cv->animate_to({c.x, c.y, c.zoom});
    if (c.focused.empty() || c.scroll <= 0 || !runtime) return;
    restore_scroll(c.focused, c.scroll, c, 16);
}

// The words searched for, shown where they are in the window opened: its first block with them scrolled into
// view and the words selected — once the window is live (tried a few times a second).
void Model::show_words(std::string key, std::string words, int tries) {
    if (!runtime || tries <= 0) return;
    (void)runtime->after(seconds(0.15), [this, key = std::move(key), words = std::move(words), tries, alive = std::weak_ptr<int>(alive_)]() mutable {
        if (alive.expired() || !runtime) return;
        const NodeId page = page_of(*runtime, canvas_path, key);
        const NodeId doc = runtime->valid(page) ? runtime->find(runtime->path(page) + "/doc") : NodeId{};
        if (!runtime->valid(doc)) {
            show_words(std::move(key), std::move(words), tries - 1);
            return;
        }
        Invocation call;
        call.args = Value{}.set("query", words);
        const Envelope e = runtime->act(doc, "find", call);
        const Value* hits = e.data_value();
        if (!hits || !hits->list() || hits->list()->empty()) return;
        // The first block that has them: the words themselves in it, else the block.
        std::vector<NodeId> todo{runtime->find(hits->list()->front().as_string())};
        auto lower = [](std::string_view t) {
            std::string out(t);
            for (char& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return out;
        };
        const std::string w = lower(words);
        while (!todo.empty()) {
            const NodeId n = todo.back();
            todo.pop_back();
            if (!runtime->valid(n)) continue;
            const spec::SemanticNode seen = runtime->semantic_node(n);
            const auto at = lower(seen.name).find(w);
            if (at != std::string::npos && std::ranges::any_of(seen.actions, [](const spec::ActionDesc& a) { return a.name == "set_selection"; })) {
                runtime->scroll_into_view(n);
                Invocation select;
                select.args = Value{}.set("anchor", static_cast<std::int64_t>(at)).set("focus", static_cast<std::int64_t>(at + w.size()));
                (void)runtime->act(n, "set_selection", select);
                return;
            }
            const auto kids = runtime->children(n);
            for (auto it = kids.rbegin(); it != kids.rend(); ++it) todo.push_back(*it);
        }
        runtime->scroll_into_view(runtime->find(hits->list()->front().as_string()));
    });
}

// The page scrolled where it was, once the camera is there and the window live (tried a few times a second).
void Model::restore_scroll(std::string key, double scroll, Camera at, int tries) {
    if (!runtime || tries <= 0) return;
    (void)runtime->after(seconds(0.15), [this, key = std::move(key), scroll, at, tries, alive = std::weak_ptr<int>(alive_)]() mutable {
        if (alive.expired() || !runtime) return;
        const bool there = std::abs(x.peek() - at.x) < 1 && std::abs(y.peek() - at.y) < 1 && std::abs(zoom.peek() - at.zoom) < 1e-3;
        const NodeId n = page_of(*runtime, canvas_path, key);
        if (!there || !runtime->valid(n)) {
            restore_scroll(std::move(key), scroll, at, tries - 1);
            return;
        }
        Invocation call;
        call.args = Value{}.set("x", 0.0).set("y", scroll);
        (void)runtime->act(n, "scroll_to", call);
    });
}

namespace {

// Moves made as one step of the program's history (Runtime::taken_back_by): undone back to `from`, redone to `to`.
void moved_as_one_step(omui::Runtime* rt, const std::string& scene_name, std::vector<std::pair<std::string, omui::world::WorldRect>> from,
                       std::vector<std::pair<std::string, omui::world::WorldRect>> to, Model& model) {
    if (!rt) return;
    auto put = [scene_name, &model](const std::vector<std::pair<std::string, omui::world::WorldRect>>& where) {
        omui::world::Scene& s = omui::world::scene(scene_name);
        for (const auto& [key, r] : where)
            if (auto n = s.find(key)) s.move(*n, r.x, r.y);
        model.keep();
    };
    rt->taken_back_by([put, from] { put(from); }, [put, to] { put(to); });
}

}  // namespace

std::size_t Model::tidy(double aspect) {
    omui::world::Scene& s = omui::world::scene(scene);
    if (!(aspect > 0)) aspect = 16.0 / 9.0;
    const Geometry g;
    struct Space {
        omui::world::NodeRef               node;
        std::vector<omui::world::NodeRef>  windows;
        double                             w = 0, h = 0;
        std::vector<std::array<double, 2>> at;  // each window's place inside
    };
    std::vector<Space> spaces;
    s.each([&](omui::world::NodeRef n) {
        if (s.kind_name(n) != "workspace") return;
        Space sp{.node = n};
        s.carried(n, sp.windows);
        std::erase_if(sp.windows, [&](omui::world::NodeRef m) { return s.kind_name(m) != "window"; });
        // In reading order as they lie now (a person's arrangement kept as an order).
        std::ranges::sort(sp.windows, [&](omui::world::NodeRef a, omui::world::NodeRef b) {
            const auto ra = s.rect(a), rb = s.rect(b);
            return ra.y != rb.y ? ra.y < rb.y : ra.x < rb.x;
        });
        spaces.push_back(std::move(sp));
    });
    if (spaces.empty()) return 0;
    std::vector<std::pair<std::string, omui::world::WorldRect>> before, after;
    // Each workspace: masonry in c columns, c chosen for its shape.
    for (Space& sp : spaces) {
        const std::size_t n = std::max<std::size_t>(1, sp.windows.size());
        double best = std::numeric_limits<double>::infinity();
        for (std::size_t c = 1; c <= n; ++c) {
            std::vector<double> col(c, g.label + g.margin);
            std::vector<std::array<double, 2>> at;
            for (omui::world::NodeRef m : sp.windows) {
                const auto k = static_cast<std::size_t>(std::ranges::min_element(col) - col.begin());
                at.push_back({g.margin + static_cast<double>(k) * (g.window + g.gap), col[k]});
                col[k] += s.rect(m).height + g.gap;
            }
            const double w = 2 * g.margin + static_cast<double>(c) * g.window + static_cast<double>(c - 1) * g.gap;
            const double h = *std::ranges::max_element(col) - g.gap + g.margin;
            const double miss = std::abs(std::log((w / h) / aspect));
            if (miss < best) best = miss, sp.w = w, sp.h = h, sp.at = std::move(at);
        }
    }
    // The workspaces in rows: as many to a row as make the wall that shape.
    double best = std::numeric_limits<double>::infinity();
    std::size_t per = spaces.size();
    for (std::size_t r = 1; r <= spaces.size(); ++r) {
        double width = 0, height = 0, x = 0, row = 0;
        for (std::size_t i = 0; i < spaces.size(); ++i) {
            if (i > 0 && i % r == 0) height += row + g.between, x = 0, row = 0;
            x += spaces[i].w + (i % r ? g.between : 0);
            width = std::max(width, x);
            row = std::max(row, spaces[i].h);
        }
        height += row;
        const double miss = std::abs(std::log((width / height) / aspect));
        if (miss < best) best = miss, per = r;
    }
    double x = 0, y = 0, row = 0;
    for (std::size_t i = 0; i < spaces.size(); ++i) {
        if (i > 0 && i % per == 0) y += row + g.between, x = 0, row = 0;
        const Space& sp = spaces[i];
        auto place = [&](omui::world::NodeRef n, double nx, double ny, float w, float h) {
            before.emplace_back(std::string(s.key(n)), s.rect(n));
            s.move(n, nx, ny);
            s.resize(n, w, h);
            after.emplace_back(std::string(s.key(n)), s.rect(n));
        };
        place(sp.node, x, y, static_cast<float>(sp.w), static_cast<float>(sp.h));
        for (std::size_t k = 0; k < sp.windows.size(); ++k) {
            const auto r = s.rect(sp.windows[k]);
            place(sp.windows[k], x + sp.at[k][0], y + sp.at[k][1], static_cast<float>(r.width), static_cast<float>(r.height));
        }
        x += sp.w + g.between;
        row = std::max(row, sp.h);
    }
    // Sizes changed too (a workspace fits its windows): undo and redo put both back.
    if (runtime) {
        const std::string name = scene;
        auto put = [name, this](const std::vector<std::pair<std::string, omui::world::WorldRect>>& where) {
            omui::world::Scene& sc = omui::world::scene(name);
            for (const auto& [key, r] : where)
                if (auto n = sc.find(key)) {
                    sc.move(*n, r.x, r.y);
                    sc.resize(*n, static_cast<float>(r.width), static_cast<float>(r.height));
                }
            keep();
        };
        runtime->taken_back_by([put, before] { put(before); }, [put, after] { put(after); });
    }
    if (omui::Canvas* c = canvas()) {
        const auto b = s.bounds();
        const omui::Size vp = c->viewport();
        c->animate_to(omui::world::Camera::fit(b, vp, 32, 0.02, 4));
    }
    keep();
    return after.size();
}

std::expected<std::size_t, std::string> Model::align(std::string_view how) {
    omui::Canvas* c = canvas();
    if (!c) return std::unexpected(std::string("the wall is not shown"));
    omui::world::Scene& s = omui::world::scene(scene);
    std::vector<omui::world::NodeRef> chosen;
    for (const std::string& k : c->selection())
        if (auto n = s.find(k); n && s.kind_name(*n) == "window") chosen.push_back(*n);
    if (chosen.size() < 2) return std::unexpected(std::string("select two windows or more first (click, then shift-click)"));
    std::vector<std::pair<std::string, omui::world::WorldRect>> before, after;
    for (auto n : chosen) before.emplace_back(std::string(s.key(n)), s.rect(n));
    double left = 1e300, top = 1e300, right = -1e300, bottom = -1e300;
    for (auto n : chosen) {
        const auto r = s.rect(n);
        left = std::min(left, r.x), top = std::min(top, r.y), right = std::max(right, r.right()), bottom = std::max(bottom, r.bottom());
    }
    if (how == "left" || how == "right" || how == "top" || how == "bottom") {
        for (auto n : chosen) {
            const auto r = s.rect(n);
            s.move(n, how == "left" ? left : how == "right" ? right - r.width : r.x, how == "top" ? top : how == "bottom" ? bottom - r.height : r.y);
        }
    } else if (how == "across" || how == "down") {
        const bool across = how == "across";
        std::ranges::sort(chosen, [&](auto a, auto b) { return across ? s.rect(a).x < s.rect(b).x : s.rect(a).y < s.rect(b).y; });
        double total = 0;
        for (auto n : chosen) total += across ? s.rect(n).width : s.rect(n).height;
        const double space = ((across ? right - left : bottom - top) - total) / static_cast<double>(chosen.size() - 1);
        double at = across ? left : top;
        for (auto n : chosen) {
            const auto r = s.rect(n);
            s.move(n, across ? at : r.x, across ? r.y : at);
            at += (across ? r.width : r.height) + std::max(space, Geometry{}.gap);
        }
    } else {
        return std::unexpected(std::format("`{}` is not a way to align: left, right, top, bottom, across, down", how));
    }
    for (auto n : chosen) after.emplace_back(std::string(s.key(n)), s.rect(n));
    moved_as_one_step(runtime, scene, std::move(before), std::move(after), *this);
    keep();
    return chosen.size();
}

bool Model::back() {
    if (back_.empty()) return false;
    forward_.push_back(here());
    go(back_.back());
    back_.pop_back();
    return true;
}

bool Model::forward() {
    if (forward_.empty()) return false;
    back_.push_back(here());
    go(forward_.back());
    forward_.pop_back();
    return true;
}

std::vector<Model::Found> Model::search(std::string_view words, std::size_t limit) const {
    auto lower = [](std::string_view s) {
        std::string out(s);
        for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    };
    const std::string w = lower(words);
    std::vector<Found> titles, paths, texts;
    if (w.empty()) return {};
    for (const Document& d : docs) {
        const std::string text = this->text(d.key);
        auto first_line = [&] {
            const auto nl = text.find('\n');
            return text.substr(0, std::min<std::size_t>(nl == std::string::npos ? text.size() : nl, 120));
        };
        if (lower(d.title).contains(w)) titles.push_back({d.key, d.title, d.path, first_line()});
        else if (lower(d.path).contains(w)) paths.push_back({d.key, d.title, d.path, first_line()});
        else if (const auto at = lower(text).find(w); at != std::string::npos) {
            const auto from = text.rfind('\n', at), to = text.find('\n', at);
            const std::size_t b = from == std::string::npos ? 0 : from + 1, e = to == std::string::npos ? text.size() : to;
            std::string line = text.substr(b, std::min<std::size_t>(e - b, 160));
            texts.push_back({d.key, d.title, d.path, std::move(line)});
        }
    }
    std::vector<Found> out;
    for (auto* list : {&titles, &paths, &texts})
        for (Found& f : *list)
            if (out.size() < limit) out.push_back(std::move(f));
    return out;
}

std::filesystem::path Model::layout_file() const { return folder.empty() ? std::filesystem::path{} : folder / ".openmui" / "wall.layout.omui.json"; }

bool Model::restore() {
    const auto file = layout_file();
    if (file.empty()) return false;
    std::ifstream in(file, std::ios::binary);
    if (!in) return false;
    const std::string text{std::istreambuf_iterator<char>(in), {}};
    auto v = parse_json(text);
    if (!v || !v->map()) {
        said.set(std::format("{} did not read: the arrangement starts afresh", utf8(file.filename())));
        return false;
    }
    world::Scene& s = world::scene(scene);
    if (const Value* nodes = v->get("nodes"); nodes && nodes->map())
        for (const auto& [key, at] : *nodes->map())
            if (const auto n = s.find(key); n && at.list() && at.list()->size() == 2) s.move(*n, (*at.list())[0].as_double(), (*at.list())[1].as_double());
    if (const Value* c = v->get("camera"); c && c->list() && c->list()->size() == 3) {
        x.set((*c->list())[0].as_double());
        y.set((*c->list())[1].as_double());
        zoom.set((*c->list())[2].as_double());
        return true;
    }
    return false;
}

void Model::keep() {
    if (layout_file().empty() || !runtime || keep_timer_) return;
    keep_timer_ = runtime->after(seconds(0.5), [this, alive = std::weak_ptr<int>(alive_)] {
        if (alive.expired()) return;
        keep_timer_ = 0;
        const world::Scene& s = world::scene(scene);
        Value nodes = Value(Value::Map{});
        s.each_unordered([&](world::NodeRef n) {
            const auto r = s.rect(n);
            nodes.set(s.key(n), Value(Value::List{Value(r.x), Value(r.y)}));
        });
        Value v;
        v.set("about", "The document wall's arrangement and camera for this folder (omui-wall writes it; delete it to start afresh).");
        v.set("camera", Value(Value::List{Value(x.peek()), Value(y.peek()), Value(zoom.peek())})).set("nodes", std::move(nodes));
        const auto file = layout_file();
        std::error_code ec;
        std::filesystem::create_directories(file.parent_path(), ec);
        if (auto w = mcpplibs::os::fs::write_atomic(file, v.json()); !w) said.set(std::format("not kept: {}", w.error().message()));
    });
}

std::expected<void, std::string> Model::save_document(std::string_view key) {
    const Document* d = document(key);
    if (!d || folder.empty()) return std::unexpected(std::string("nowhere to write it"));
    const auto file = folder / std::filesystem::path(std::u8string(d->path.begin(), d->path.end()));
    if (auto w = mcpplibs::os::fs::write_atomic(file, text(key)); !w) {
        said.set(std::format("{} not saved: {}", d->path, w.error().message()));
        return std::unexpected(w.error().message());
    }
    said.set(std::format("{} saved", d->path));
    return {};
}

omui::Canvas* Model::canvas() const { return runtime ? runtime->component<omui::Canvas>(runtime->find(canvas_path)) : nullptr; }

const Document* Model::at_path(std::string_view path) const {
    for (const Document& d : docs)
        if (d.path == path) return &d;
    return nullptr;
}

std::optional<std::string> Model::follow(std::string_view from_key, std::string_view href) {
    if (!runtime || href.empty() || href.starts_with('#')) return std::nullopt;  // a heading of its own: the document scrolls
    if (href.find("://") != std::string_view::npos || href.starts_with("mailto:")) {
        (void)runtime->open_url(href);
        return std::nullopt;
    }
    std::string_view target = href.substr(0, href.find('#'));
    // Relative to the window's folder: its parts, then the link's, `.` and `..` resolved.
    std::vector<std::string> parts;
    const Document* from = document(from_key);
    if (from && !target.starts_with('/'))
        for (auto p : std::views::split(std::string_view(from->path), '/')) parts.emplace_back(std::string_view(p));
    if (!parts.empty()) parts.pop_back();  // the file itself
    for (auto p : std::views::split(target, '/')) {
        const std::string_view part(p);
        if (part.empty() || part == ".") continue;
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else {
            parts.emplace_back(part);
        }
    }
    std::string path;
    for (const std::string& p : parts) path += path.empty() ? p : "/" + p;
    const Document* to = at_path(path);
    if (!to) return std::nullopt;
    open(to->key);  // back() returns here
    return to->key;
}

void Model::stream(std::size_t windows, double rate) {
    stop_stream();
    if (!runtime || windows == 0) return;
    omui::world::Scene& s = omui::world::scene(scene);
    // The windows nearest the view's centre: what a person watches.
    std::vector<std::pair<double, std::string>> near;
    s.each_unordered([&](omui::world::NodeRef n) {
        if (s.kind_name(n) != "window") return;
        const auto r = s.rect(n);
        const double dx = r.x + r.width / 2 - x.peek(), dy = r.y + r.height / 2 - y.peek();
        near.emplace_back(dx * dx + dy * dy, std::string(s.key(n)));
    });
    std::ranges::sort(near);
    for (std::size_t i = 0; i < std::min(windows, near.size()); ++i) streams_.push_back({near[i].second});
    rate_ = rate;
    last_ = runtime->now();
    // Text arriving reads smoothly at 60 frames a second; 24 at the least (D-70).
    pace_ = runtime->demand_frames({.min_hz = 24, .max_hz = 60, .preferred_hz = 60, .reason = "streaming"});
    schedule();
}

void Model::stop_stream() {
    pace_.release();
    if (runtime && timer_) runtime->cancel(timer_);
    timer_ = 0;
    // What was written stays; the textures show all of it.
    if (runtime)
        if (auto* s = omui::world::find_scene(scene))
            for (const Stream& st : streams_)
                if (auto n = s->find(st.key)) s->touch(*n);
    streams_.clear();
}

void Model::schedule() {
    timer_ = runtime->after(omui::seconds(0.05), [this, token = std::weak_ptr<int>(alive_)] {
        if (token.expired()) return;
        timer_ = 0;
        tick();
        if (!streams_.empty()) schedule();
    });
}

// Twenty times a second: each stream's tokens due since the last tick. A
// live window gets them at once (its document appends, parsing only the
// tail); a window drawn as a texture is made stale at most four times a
// second — its texture is drawn again on the pool, the old one meanwhile.
void Model::tick() {
    const omui::seconds now = runtime->now();
    const double dt = (now - last_).count();
    last_ = now;
    omui::world::Scene* s = omui::world::find_scene(scene);
    for (Stream& st : streams_) {
        st.owed += rate_ * dt;
        const auto n = static_cast<std::uint64_t>(st.owed);
        if (n == 0) continue;
        st.owed -= static_cast<double>(n);
        std::string more;
        for (std::uint64_t i = 0; i < n; ++i) more += token(st.key, st.next++);
        tokens_ += n;
        auto text = live(st.key);
        text.set(text.peek() + more);
        if (s && now.count() - st.touched >= 0.25)
            if (auto node = s->find(st.key)) {
                s->touch(*node);
                st.touched = now.count();
            }
    }
}

void register_all() {
    omui::register_canvas_component();
    omui::register_canvas_kit();
    omui::register_markdown_component();
    for (auto [text, file] : {std::pair{group_drag_behavior, std::string_view("group-drag.behavior.omui.toml")},
                              std::pair{views_behavior, std::string_view("views.behavior.omui.toml")}}) {
        // Checked when the program is built (the tests load them); a broken one is a bug, said once.
        if (auto loaded = omui::world::load_behaviors(text, file); !loaded)
            for (const auto& p : loaded.error()) omui::warn("wall: {}", p.text());
    }
}

namespace {

// The HUD (immediate mode): what the last frames cost, what the canvas drew
// by tier, the textures, the streams. Built again four times a second.
void hud(omui::Ui& ui, Model& model) {
    using namespace omui;
    ui.rebuild_after(seconds(0.25));
    if (!model.hud.get()) return;
    const Canvas* c = model.canvas();
    if (!c || !model.runtime) return;
    const auto history = model.runtime->frame_history();
    std::vector<double> ms;
    for (const auto& f : history) ms.push_back(f.timings.total);
    std::vector<double> sorted = ms;
    std::ranges::sort(sorted);
    const double p50 = sorted.empty() ? 0 : sorted[sorted.size() / 2], p99 = sorted.empty() ? 0 : sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
    const auto& st = c->stats();
    const auto tx = c->texture_stats();
    const std::size_t drawn = st.textured;
    ui.column({.id = "hud", .width = 340, .padding = Edges(10.0f, 12.0f), .gap = 4, .background = Fill("surface"), .border_color = Fill("border"),
               .border_width = 1, .radius = 10, .shadow = "md"},
              [&] {
                  ui.text(std::format("{:.0f} fps · frame {:.2f} ms · p99 {:.2f} ms", model.runtime->frame_rate(), p50, p99), {.id = "frame", .style = "label"});
                  // What frames are asked for, and why (D-70).
                  const FrameDemand& want = model.runtime->frame_demand();
                  const double hz = model.runtime->display_hz();
                  ui.text(want.idle() ? std::format("idle · display {:.0f} Hz", hz)
                                      : std::format("asks {} · {} · display {:.0f} Hz",
                                                    std::isinf(want.wanted()) ? std::string("the fastest") : std::format("{:.0f} Hz", want.wanted()),
                                                    want.reason, hz),
                          {.id = "pacing", .style = "caption"});
                  // The last frames, against a 120 Hz frame's 8.3 ms.
                  ui.add(painter({.id = "frames",
                                  .width = 316,
                                  .height = 44,
                                  .label = "Frame times",
                                  .paint = [ms](PaintContext& p) {
                                      const float w = p.size.width, h = p.size.height;
                                      p.list.fill_rect({0, 0, w, h}, p.theme.color("surface_variant"));
                                      const float budget = h - static_cast<float>(8.33 / 16.67) * h;
                                      p.list.fill_rect({0, budget, w, 1}, p.theme.color("warning"));
                                      const std::size_t n = std::min<std::size_t>(ms.size(), 158);
                                      for (std::size_t i = 0; i < n; ++i) {
                                          const double v = ms[ms.size() - n + i];
                                          const float bh = std::min(h, static_cast<float>(v / 16.67) * h);
                                          p.list.fill_rect({static_cast<float>(i) * 2, h - bh, 1.5f, bh}, p.theme.color(v > 8.33 ? "danger" : "accent"));
                                      }
                                  }}));
                  ui.text(std::format("{} on screen · {:.3f} ms to query, {:.3f} ms to draw", st.visible, st.query_ms, st.paint_ms),
                          {.id = "visible", .style = "caption"});
                  ui.text(std::format("T0 {} · texture {} ({} stretched) · live {}", st.custom, drawn, st.stretched, st.promoted),
                          {.id = "tiers", .style = "caption"});
                  ui.text(std::format("textures {} · {:.1f} MiB · {} drawing · {} in, {} out", tx.textures, static_cast<double>(tx.bytes) / (1 << 20), tx.jobs,
                                      tx.entered, tx.evicted),
                          {.id = "textures", .style = "caption"});
                  ui.text(std::format("sharp {:.0f}% · zoom {:.3g}% · {} documents", drawn ? 100.0 * static_cast<double>(drawn - st.stretched) / static_cast<double>(drawn) : 100.0,
                                      model.zoom.peek() * 100, model.docs.size()),
                          {.id = "sharp", .style = "caption"});
                  if (model.streaming())
                      ui.text(std::format("streaming into {} windows · {} tokens", model.streaming(), model.tokens()), {.id = "streams", .style = "caption"});
                  ui.text("drag a label or a title · 1: one to one · 2: all · H", {.id = "keys", .style = "caption", .color = Fill("text_muted")});
              });
}

}  // namespace

omui::AppSpec app(Model& model) {
    omui::AppSpec a;
    a.id = "org.openmui.wall";
    a.title = model.title;
    a.size = {1440, 900};
    a.setup = [&model](omui::Runtime& rt) {
        model.runtime = &rt;
        register_all();
        build(model, omui::world::scene(model.scene));
        model.restored = model.restore();  // the folder's arrangement and camera, as last left
    };
    for (auto [name, doc] : {std::pair{"wall.back", "Back to where the camera was before the last document opened (Alt+←)."},
                             {"wall.forward", "Forward again (Alt+→)."}})
        a.verbs.push_back({{name, doc, omui::Effect::read, {}, {}}, [&model, n = std::string_view(name)](const omui::Value&) -> omui::VerbResult {
                               const bool went = n == "wall.back" ? model.back() : model.forward();
                               if (!went) return std::unexpected(std::string(n == "wall.back" ? "nothing to go back to" : "nothing to go forward to"));
                               return omui::Value{};
                           }});
    a.verbs.push_back({{"wall.search", "The documents with these words: titles first, then paths, then text.", omui::Effect::read, {}, {{"words", "string"}}},
                       [&model](const omui::Value& args) -> omui::VerbResult {
                           omui::Value::List out;
                           for (const auto& f : model.search(args.get("words") ? args.get("words")->as_string() : ""))
                               out.push_back(omui::Value{}.set("key", f.key).set("title", f.title).set("path", f.path).set("line", f.line));
                           return omui::Value(std::move(out));
                       }});
    a.verbs.push_back({{"wall.open", "Brings a document into view to read it (by its key or its path).", omui::Effect::read, {}, {{"document", "string"}}},
                       [&model](const omui::Value& args) -> omui::VerbResult {
                           const std::string_view d = args.get("document") ? args.get("document")->as_string() : "";
                           const Document* doc = model.document(d) ? model.document(d) : model.at_path(d);
                           if (!doc) return std::unexpected(std::format("no document `{}`", d));
                           model.open(doc->key);
                           return omui::Value(doc->key);
                       }});
    a.verbs.push_back({{"wall.tidy", "Tidies the wall: each workspace's windows packed in columns, the workspaces in rows, the shape of the window it is seen in; one step of history.",
                        omui::Effect::reversible, {}, {}},
                       [&model](const omui::Value&) -> omui::VerbResult {
                           const omui::Canvas* c = model.canvas();
                           const omui::Size vp = c ? c->viewport() : omui::Size{16, 9};
                           return omui::Value(static_cast<std::int64_t>(model.tidy(vp.height > 0 ? vp.width / vp.height : 16.0 / 9.0)));
                       }});
    a.verbs.push_back({{"wall.align", "Lines up the selected windows: how is left, right, top, bottom, across (evenly left to right) or down.", omui::Effect::reversible,
                        {}, {{"how", "string"}}},
                       [&model](const omui::Value& args) -> omui::VerbResult {
                           const omui::Value* how = args.get("how");
                           auto r = model.align(how ? how->as_string() : "");
                           if (!r) return std::unexpected(r.error());
                           return omui::Value(static_cast<std::int64_t>(*r));
                       }});
    a.verbs.push_back({{"wall.stream", "Writes into the windows nearest the view's centre at a language model's pace.", omui::Effect::reversible, {},
                        {{"windows", "i64"}, {"rate", "f64"}}},
                       [&model](const omui::Value& args) -> omui::VerbResult {
                           const auto n = args.get("windows") ? args.get("windows")->as_int(0) : 0;
                           const double rate = args.get("rate") ? args.get("rate")->as_double(50) : 50;
                           if (n < 0 || n > 1000 || !(rate > 0)) return std::unexpected(std::string("windows is 0 to 1000 and rate above 0"));
                           model.stream(static_cast<std::size_t>(n), rate);
                           return omui::Value(static_cast<std::int64_t>(model.streaming()));
                       }});
    a.verbs.push_back({{"wall.stop", "Stops streaming; what was written stays.", omui::Effect::reversible, {}, {}},
                       [&model](const omui::Value&) -> omui::VerbResult {
                           model.stop_stream();
                           return omui::Value{};
                       }});
    a.view = [&model] {
        using namespace omui;
        // The HUD over the canvas, its own size at the corner (not stretched over the wall, where it would take its input).
        Element wall = canvas({.id = "wall",
                               .behaviors = std::vector<std::string>{"select", "drag", "snap", "minimap", "group-drag", "views"},
                               .label = model.title,
                               .max_zoom = 8,
                               .min_zoom = 0.004,
                               .scene = model.scene,
                               .start = model.restored ? std::string() : std::string("fit"),
                               .x = model.x,
                               .y = model.y,
                               .zoom = model.zoom,
                               // A double-click (or Enter) on a window opens it to read.
                               .on_activated = [&model](std::string key) { if (model.document(key)) model.open(key); },
                               .on_camera_changed = [&model](double, double, double) { model.keep(); },
                               .on_moved = [&model](double, double, std::vector<std::string>) { model.keep(); },
                               .width = Dim(layout::pct(100)),
                               .height = Dim(layout::pct(100)),
                               .on = {{"hud.toggle", [&model](const Value&) { model.hud.set(!model.hud.peek()); }},
                                      {"key_down", [&model](const Value& e) {
                                           const std::string_view key = e.get("key") ? e.get("key")->as_string() : "";
                                           const Value* mods = e.get("mods");
                                           auto has = [&](std::string_view m) {
                                               return mods && mods->list() && std::ranges::any_of(*mods->list(), [&](const Value& x) { return x.as_string() == m; });
                                           };
                                           if (has("alt") && key == "Left") (void)model.back();
                                           else if (has("alt") && key == "Right") (void)model.forward();
                                           else if ((has("ctrl") || has("meta")) && key == "F") model.open_search();
                                           else if (key == "Slash" && !has("ctrl")) model.open_search();
                                       }}}});
        // How to find one's way, and back: always at hand (top right).
        Element bar = row({.id = "toolbar", .padding = Edges(6.0f, 8.0f), .gap = 6, .align = "center", .background = "surface", .border_color = "border",
                           .border_width = 1, .radius = 10, .shadow = "md"},
                          {button({.id = "back", .disabled = computed([&model] { (void)model.x.get(); return !model.can_back(); }), .text = "Back",
                                   .variant = ButtonVariant::ghost, .on_activated = [&model] { (void)model.back(); }}),
                           button({.id = "forward", .disabled = computed([&model] { (void)model.x.get(); return !model.can_forward(); }), .text = "Forward",
                                   .variant = ButtonVariant::ghost, .on_activated = [&model] { (void)model.forward(); }}),
                           button({.id = "search", .text = "Search", .variant = ButtonVariant::secondary, .on_activated = [&model] { model.open_search(); }}),
                           button({.id = "tidy", .text = "Tidy", .variant = ButtonVariant::ghost, .on_activated = [&model] {
                                       const omui::Canvas* c = model.canvas();
                                       const omui::Size vp = c ? c->viewport() : omui::Size{16, 9};
                                       (void)model.tidy(vp.height > 0 ? vp.width / vp.height : 16.0 / 9.0);
                                   }}),
                           text({.id = "how", .text = "drag · wheel to zoom · double-click to read · 2 all", .style = "caption", .color = "text_muted"})});
        // Search: the words, and what has them; Enter or a click goes there, Esc closes it.
        Element finder = column({.id = "finder", .width = Dim(520.0f), .padding = Edges(12.0f), .gap = 8, .background = "surface", .border_color = "border",
                                 .border_width = 1, .radius = 12, .shadow = "lg"},
                                {text_box({.id = "query", .label = "Search the documents", .placeholder = "Words in a title, a path or the text",
                                           .text = model.query,
                                           .on_changed = [&model](std::string v) { model.query.set(std::move(v)); },
                                           .on_submitted = [&model](std::string v) {
                                               const auto found = model.search(v, 1);
                                               if (found.empty()) return;
                                               model.close_search();
                                               model.open(found.front().key, v);
                                           }}),
                                 dynamic([&model] {
                                     std::vector<Element> out;
                                     const auto found = model.search(model.query.get());
                                     if (!model.query.get().empty() && found.empty())
                                         out.push_back(text({.id = "none", .text = "Nothing has these words.", .style = "caption", .color = "text_muted"}));
                                     for (std::size_t i = 0; i < found.size(); ++i) {
                                         const auto& f = found[i];
                                         out.push_back(button({.id = std::format("result{}", i), .text = std::format("{} — {}", f.title, f.path),
                                                               .variant = ButtonVariant::ghost, .on_activated = [&model, key = f.key] {
                                                                   model.close_search();
                                                                   model.open(key, model.query.peek());
                                                               }}));
                                         out.push_back(text({.id = std::format("line{}", i), .text = f.line, .style = "caption", .color = "text_muted", .max_lines = 1}));
                                     }
                                     return out;
                                 })});
        finder.on("key_down", [&model](const Value& e) {
            if (e.get("key") && e.get("key")->as_string() == "Escape") model.close_search();
        });
        finder.prop("display", derive([&model] { return std::string(model.searching.get() ? "flex" : "none"); }));
        return stack({.id = "app", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"},
                     {std::move(wall),
                      // The HUD reads; it takes no pointer: a click on what it covers reaches the wall.
                      immediate([&model](Ui& ui) { hud(ui, model); }, {.id = "hud-layer", .margin = Edges(12)}).prop("pointer_events", Source{Value("none"), {}, {}}),
                      // Layers over the wall that take no pointer themselves, only their controls do.
                      column({.id = "top", .width = Dim(layout::pct(100)), .padding = Edges(12.0f), .gap = 8, .align = "end"}, {std::move(bar)})
                          .prop("pointer_events", Source{Value("children"), {}, {}}),
                      column({.id = "middle", .width = Dim(layout::pct(100)), .padding = Edges(Insets{80, 0, 0, 0}), .align = "center"}, {std::move(finder)})
                          .prop("pointer_events", Source{Value("children"), {}, {}})});
    };
    return a;
}

}  // namespace omui::wall
