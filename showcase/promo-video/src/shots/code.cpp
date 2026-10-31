// The code (0:08–0:16): tally.ts is typed on the left, and the counter it
// builds appears on the right as each of its lines arrives. Then the code
// turns into C++, Python and Lua on the beat, and the counter does not move
// by a pixel — each version runs, in its own runtime (the script hosts:
// QuickJS-ng for TypeScript, PocketPy for Python, Lua; C++ compiled into
// the film), and the hashes of its semantic tree and its pixels are shown
// under it. They are the same four times (the CTS's S level); the last beat
// shows the four side by side.
module openmui.showcase.promo_video;

import std;
import hanim;
import openmui;
import openmui.abi;
import openmui.lang;
import openmui.lang.lua;
import openmui.lang.js;
import openmui.lang.python.embedded;

namespace omui::promo {

namespace {

constexpr std::array<std::string_view, 4> languages{"ts", "cpp", "py", "lua"};
constexpr std::array<std::string_view, 4> language_names{"TypeScript", "C++", "Python", "Lua"};
constexpr std::array<std::string_view, 4> file_names{"tally.ts", "tally.cpp", "tally.py", "tally.lua"};
constexpr Size tally_size{360, 240};
constexpr std::array<std::string_view, 6> tally_ids{"count", "progress", "status", "controls/minus", "controls/plus", "controls/reset"};

// The counter as one language's version drew it: its pixels, the hashes
// of its tree and pixels, where its nodes are.
struct Tally {
    std::shared_ptr<const raster::Surface> pixels;
    std::uint64_t                          tree = 0, picture = 0;
    std::map<std::string, Rect>            bounds;  // by id (tally_ids), logical
    std::string                            error;
};

std::string_view source_of(std::string_view language) {
    static const Files files;
    static std::map<std::string, std::string, std::less<>> texts;
    static std::mutex m;
    std::scoped_lock g(m);
    auto it = texts.find(language);
    if (it == texts.end()) {
        auto t = files.read(std::format("code/tally.{}", language));
        it = texts.emplace(std::string(language), t ? *t : std::string{}).first;
    }
    return it->second;
}

// Each version run once a process, at a scale: its host, its script.
Tally run_tally(std::string_view language, float scale) {
    Tally out;
    typeset::FontCollection fonts;
    if (auto r = fonts.add_defaults(); !r) warn("promo: the default fonts did not load: {}", r.error());
    Headless h(HeadlessOptions{.size = tally_size, .scale = scale, .background = hex(ink::paper), .theme = blueprint(), .fonts = &fonts, .app = "openmui-promo-tally",
                               .complete_frames = true});
    if (language == "cpp") {
        h.mount("main", tally(signal(0), signal(10)));
        h.settle();
    } else {
        auto handle = abi::attach(h.runtime());
        {
            lang::Host host(handle);
            lang::Session session(host);
            std::string code(source_of(language)), name(std::format("tally.{}", language));
            std::unique_ptr<lang::lua::Script> lua;
            std::unique_ptr<lang::js::Script> js;
            std::unique_ptr<lang::python::embedded::Script> py;
            std::future<lang::Result<void>> ran;
            std::vector<std::string> errors;
            auto keep = [&](std::string_view m) { errors.emplace_back(m); };
            if (language == "lua") {
                lua = std::make_unique<lang::lua::Script>(session);
                lua->on_error(keep);
                ran = lua->run(std::move(code), "@" + name);
            } else if (language == "ts") {
                auto compiled = lang::js::compile_typescript_source(code, name);
                if (!compiled) {
                    out.error = compiled.error();
                    abi::detach(handle);
                    return out;
                }
                js = std::make_unique<lang::js::Script>(session);
                js->on_error(keep);
                ran = js->run(std::move(*compiled), name);
            } else {
                py = std::make_unique<lang::python::embedded::Script>(session);
                py->on_error(keep);
                ran = py->run(std::move(code), name);
            }
            while (ran.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
                h.step();
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            if (auto r = ran.get(); !r) out.error = r.error().text();
            for (int i = 0; i < 4000 && !session.idle(); ++i) {
                h.step();
                std::this_thread::sleep_for(std::chrono::microseconds(200));
            }
            h.settle();
            if (!errors.empty() && out.error.empty()) out.error = errors.front();
        }
        abi::detach(handle);
    }
    out.pixels = std::make_shared<raster::Surface>(h.pixels());
    out.picture = pixel_hash(*out.pixels);
    semantic::Tree tree(h.runtime());
    out.tree = tree.digest();
    for (std::string_view id : tally_ids)
        if (const NodeId n = h.runtime().find(std::format("main/{}", id))) out.bounds[std::string(id)] = h.runtime().bounds(n);
    if (out.bounds.size() != tally_ids.size() && out.error.empty()) out.error = std::format("{}: the counter has {} of its {} nodes", language, out.bounds.size(), tally_ids.size());
    return out;
}

// Run once a process, one at a time (the script hosts' interpreters are the process's: PocketPy keeps
// sixteen VMs at most), whichever player asks first; the others wait for it.
const Tally& tally_of(std::string_view language, float scale) {
    static std::mutex m;
    static std::map<std::pair<std::string, float>, std::shared_ptr<const Tally>> cache;
    std::scoped_lock g(m);
    auto it = cache.find({std::string(language), scale});
    if (it == cache.end()) {
        auto made = std::make_shared<const Tally>(run_tally(language, scale));
        if (!made->error.empty()) warn("promo: the {} counter: {}", language, made->error);
        it = cache.emplace(std::pair{std::string(language), scale}, std::move(made)).first;
    }
    return *it->second;
}

std::size_t typed_chars(std::string_view code) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < code.size(); ++i) {
        const auto c = static_cast<unsigned char>(code[i]);
        if (code[i] != '\n' && (c & 0xC0) != 0x80) ++n;
    }
    return n;
}

struct State {
    double t = 0;
    int    language = 0;       // shown now
    double switched = -1;      // when it last changed (shot seconds)
    double typed_from = 0.3, per_char = 0.003;
    std::map<std::string, double> revealed;  // when each node's line is typed
    Rect   device, panel;      // screen
    float  shown_scale = 1.8f;
    std::array<render::ImageRef, 4> images{};
    std::array<const Tally*, 4> tallies{};
    Labels labels;
};

class Code final : public Shot {
public:
    explicit Code(const ShotContext& c) : c_(c), s_(std::make_shared<State>()) {
        const bool portrait = c.portrait();
        const Size screen = c.size;
        size_ = 16.0f;
        State& s = *s_;
        s.shown_scale = portrait ? 2.2f : 1.8f;
        s.panel = portrait ? Rect{54, 120, screen.width - 108, 720} : Rect{96, 96, 1010, 790};
        const float dw = tally_size.width * s.shown_scale, dh = tally_size.height * s.shown_scale;
        s.device = portrait ? Rect{screen.width / 2 - dw / 2, 930, dw, dh} : Rect{1180 + (screen.width - 1180 - 96 - dw) / 2, 170, dw, dh};
        for (std::size_t i = 0; i < languages.size(); ++i) code_[i] = shown_code(source_of(languages[i]), languages[i]);
        // How fast tally.ts is typed: its characters over the clip's `typed` keys.
        const auto keys = c.steps("typed");
        if (keys.size() >= 2) {
            s.typed_from = keys.front().first;
            s.per_char = (keys.back().first - keys.front().first) / static_cast<double>(std::max<std::size_t>(1, typed_chars(code_[0])));
        }
        // Each node appears as the line that makes it is typed.
        for (std::string_view id : tally_ids) {
            const std::string leaf(id.substr(id.rfind('/') == std::string_view::npos ? 0 : id.rfind('/') + 1));
            const std::size_t at = code_[0].find(std::format("id: \"{}\"", leaf));
            const std::size_t line_start = at == std::string::npos ? 0 : code_[0].rfind('\n', at);
            const std::size_t end_of_line = at == std::string::npos ? 0 : std::min(code_[0].find('\n', at), code_[0].size());
            (void)line_start;
            s.revealed[std::string(id)] = s.typed_from + static_cast<double>(typed_chars(std::string_view(code_[0]).substr(0, end_of_line))) * s.per_char;
        }
        for (const auto& [t, v] : c.steps("language"))
            if (t > 0) switches_.push_back({static_cast<std::int64_t>(std::llround(t * c.rate())), static_cast<int>(std::lround(v))});
    }

    View view() override {
        State& s = *s_;
        auto st = s_;
        const bool portrait = c_.portrait();
        Element back = painter({.id = "backdrop",
                                .width = Dim(layout::pct(100)),
                                .height = Dim(layout::pct(100)),
                                .role = Role::figure,
                                .label = "The counter each version of the code draws",
                                .paint = [st](PaintContext& p) { paint(p, *st); }});
        std::vector<Element> blocks;
        for (std::size_t i = 0; i < languages.size(); ++i) {
            Element b = code_block(std::format("code-{}", languages[i]), code_[i], languages[i], size_);
            b.prop("opacity", Source(Value(i == 0 ? 1.0 : 0.0)));
            if (i == 0) {
                GlyphMotion type = GlyphMotion::typewriter(hanim::seconds(s.per_char));
                type.transition.delay = hanim::seconds(s.typed_from);
                b = animate_glyphs(std::move(b), type);
            }
            blocks.push_back(std::move(b));
        }
        Element title = row({.id = "title", .gap = 14, .align = "center"},
                            {text({.id = "file", .text = std::string(file_names[0]), .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 17.0f,
                                   .weight = 600.0f}),
                             text({.id = "language", .text = std::string(language_names[0]), .style = "code", .color = Fill(hex(ink::cyan)), .wrap = false,
                                   .size = 14.0f})});
        Element panel = column({.id = "panel", .width = Dim(s.panel.width), .padding = Edges(Insets{14, 20, 14, 20}), .gap = 14, .translate_x = s.panel.x,
                                .translate_y = s.panel.y},
                               {std::move(title), stack({.id = "blocks", .align = "start"}, std::move(blocks))});
        // Under the counter: what it is, its hashes, and each version as it is checked.
        std::vector<Element> chips;
        for (std::size_t i = 0; i < languages.size(); ++i)
            chips.push_back(text({.id = std::format("chip-{}", languages[i]), .text = std::format("{} ✓", language_names[i]), .style = "code",
                                  .color = Fill(hex(ink::muted, 0.35f)), .wrap = false, .size = 16.0f, .weight = 600.0f}));
        Element badges = column({.id = "badges", .width = Dim(portrait ? s.device.width : s.device.width), .gap = 10, .align = "start",
                                 .translate_x = s.device.x, .translate_y = s.device.y + s.device.height + 34},
                                {text({.id = "verdict", .text = "", .style = "title", .color = Fill(hex(ink::mint)), .wrap = false, .size = 34.0f, .weight = 700.0f}),
                                 text({.id = "tree", .text = "", .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 16.0f}),
                                 text({.id = "pixels", .text = "", .style = "code", .color = Fill(hex(ink::ice)), .wrap = false, .size = 16.0f}),
                                 row({.id = "chips", .gap = 22}, std::move(chips))});
        Element type = stack({.id = "type", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"}, {std::move(panel), std::move(badges)});
        return stack({.id = "code", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "start"}, {std::move(back), std::move(type)});
    }

    void mounted(Runtime& rt) override {
        State& s = *s_;
        for (std::size_t i = 0; i < languages.size(); ++i) {
            s.tallies[i] = &tally_of(languages[i], s.shown_scale * c_.scale);
            if (s.tallies[i]->pixels) s.images[i] = rt.images().add(s.tallies[i]->pixels);
        }
        backdrop_ = rt.find("main/backdrop");
        for (std::size_t i = 0; i < languages.size(); ++i) blocks_[i] = rt.find(std::format("main/type/panel/blocks/code-{}", languages[i]));
    }

    std::vector<std::int64_t> moments() const override {
        std::vector<std::int64_t> out;
        for (const auto& [f, l] : switches_) out.push_back(f);
        return out;
    }

    // A switch: the code shown scatters, the next gathers.
    void moment(Runtime& rt, std::int64_t frame) override {
        for (const auto& [f, l] : switches_) {
            if (f != frame || l > 3) continue;
            const int from = l - 1;
            if (from >= 0) rt.set(blocks_[static_cast<std::size_t>(from)], "glyph_motion", to_value(GlyphMotion::scatter(static_cast<std::uint64_t>(from + 3))));
            GlyphMotion in = GlyphMotion::gather(static_cast<std::uint64_t>(l + 11));
            in.stagger = hanim::seconds(0.00025);
            rt.set(blocks_[static_cast<std::size_t>(l)], "glyph_motion", to_value(in));
        }
    }

    void at(Runtime& rt, double t) override {
        State& s = *s_;
        s.t = t;
        int lang = 0;
        double since = -1;
        for (const auto& [f, l] : switches_)
            if (static_cast<double>(f) / c_.rate() <= t + 1e-9) lang = l, since = static_cast<double>(f) / c_.rate();
        s.language = lang;
        s.switched = since;
        const int code = std::min(lang, 3);
        for (std::size_t i = 0; i < languages.size(); ++i) {
            double o = 0;
            if (static_cast<int>(i) == code) o = 1;
            else if (lang <= 3 && static_cast<int>(i) == code - 1 && since >= 0) o = 1 - ramp(t, since + 0.45, since + 0.6);
            rt.set(blocks_[i], "opacity", Value(o));
        }
        rt.set(rt.find("main/type/panel/title/file"), "text", Value(std::string(file_names[static_cast<std::size_t>(code)])));
        rt.set(rt.find("main/type/panel/title/language"), "text", Value(std::string(language_names[static_cast<std::size_t>(code)])));
        // The hashes of the version shown, and which versions have been run and found the same.
        const Tally* shown = s.tallies[static_cast<std::size_t>(code)];
        const Tally* first = s.tallies[0];
        const bool done_typing = t >= s.typed_from + 0.2 + static_cast<double>(typed_chars(code_[0])) * s.per_char;
        const double badge = smooth(t, s.typed_from + static_cast<double>(typed_chars(code_[0])) * s.per_char, s.typed_from + 0.3 + static_cast<double>(typed_chars(code_[0])) * s.per_char);
        rt.set(rt.find("main/type/badges"), "opacity", Value(badge));
        if (shown) {
            rt.set(rt.find("main/type/badges/tree"), "text", Value(std::format("semantic tree  {:016x}", shown->tree)));
            rt.set(rt.find("main/type/badges/pixels"), "text", Value(std::format("pixels         {:016x}", shown->picture)));
        }
        // The verdict, as large as the claim: the version shown against the first.
        {
            const bool same = shown && first && shown->error.empty() && shown->tree == first->tree && shown->picture == first->picture;
            const NodeId verdict = rt.find("main/type/badges/verdict");
            rt.set(verdict, "text", Value(code == 0 ? std::string("") : same ? std::string("same tree · same pixels ✓") : std::string("differs ✗")));
            rt.set(verdict, "color", Value(std::string(same ? "#34d399" : "#fb7185")));
        }
        for (std::size_t i = 0; i < languages.size(); ++i) {
            const Tally* v = s.tallies[i];
            const bool run = static_cast<int>(i) <= lang && (i > 0 || done_typing);
            const bool same = v && first && v->error.empty() && v->tree == first->tree && v->picture == first->picture;
            const NodeId chip = rt.find(std::format("main/type/badges/chips/chip-{}", languages[i]));
            rt.set(chip, "text", Value(std::format("{} {}", language_names[i], !run ? "·" : same ? "✓" : "✗")));
            rt.set(chip, "color", Value(run ? (same ? std::string("#34d399") : std::string("#fb7185")) : std::string("#8fb3d659")));
        }
        rt.invalidate_paint(backdrop_);
    }

    std::vector<Cue> cues(double seconds) override {
        std::vector<Cue> out;
        const State& s = *s_;
        // A key every three characters while tally.ts is typed, a little different each time.
        const std::size_t n = typed_chars(code_[0]);
        std::uint64_t h = 0x9e3779b97f4a7c15ull;
        for (std::size_t k = 0; k < n; k += 3) {
            h ^= h << 13, h ^= h >> 7, h ^= h << 17;
            const double at = s.typed_from + static_cast<double>(k) * s.per_char;
            if (at >= seconds) break;
            out.push_back({at, "tick", 0.6f + static_cast<float>(h % 40) / 100.0f, static_cast<float>(static_cast<int>(h >> 8) % 60 - 30) / 100.0f,
                           0.9f + static_cast<float>((h >> 16) % 20) / 100.0f});
        }
        for (const auto& [f, l] : switches_) {
            const double at = static_cast<double>(f) / c_.rate();
            if (at < seconds) out.push_back({at, l > 3 ? "sting" : "morph", l > 3 ? 0.7f : 1.0f, 0, 1});
        }
        return out;
    }

private:
    static void paint(PaintContext& p, State& s);

    ShotContext                         c_;
    std::shared_ptr<State>              s_;
    std::array<std::string, 4>          code_;
    std::vector<std::pair<std::int64_t, int>> switches_;  // the frame each version comes in
    std::array<NodeId, 4>               blocks_{};
    NodeId                              backdrop_{};
    float                               size_ = 15.5f;
};

void Code::paint(PaintContext& p, State& s) {
    auto& list = p.list;
    const Size screen = p.size;
    paint_paper(list, screen, {static_cast<float>(-s.t * 6), 0}, 1);
    // The code's sheet.
    list.fill_round_rect(s.panel, CornerRadii::all(10), render::Paint::solid(hex(ink::panel, 0.92f)));
    list.stroke_round_rect(s.panel, CornerRadii::all(10), 1, render::Paint::solid(hex(ink::line, 0.8f)));
    list.fill_rect({s.panel.x + 1, s.panel.y + 50, s.panel.width - 2, 1}, hex(ink::line, 0.6f));
    paint_ticks(list, {s.panel.x - 8, s.panel.y - 8, s.panel.width + 16, s.panel.height + 16}, hex(ink::cyan, 0.5f), 14, 1.5f, 0);
    const Rect d = s.device;
    const int lang = s.language;
    const auto img_rect = [&](std::size_t i) { return Rect{0, 0, static_cast<float>(s.tallies[i]->pixels->width()), static_cast<float>(s.tallies[i]->pixels->height())}; };
    if (lang <= 3) {
        // The counter the version shown draws; while tally.ts is typed, a node at a time as its line arrives.
        const std::size_t i = static_cast<std::size_t>(lang);
        const Tally* v = s.tallies[i];
        list.box_shadow(d, CornerRadii::all(8), {0, 14}, 40, 0, hex(0x000000, 0.5f));
        list.fill_round_rect(d, CornerRadii::all(8), render::Paint::solid(hex(ink::paper)));
        if (v && v->pixels) {
            if (lang == 0) {
                for (const auto& [id, when] : s.revealed) {
                    const double on = smooth(s.t, when, when + 0.25);
                    const auto it = v->bounds.find(id);
                    if (on <= 0 || it == v->bounds.end()) continue;
                    const Rect b = it->second;
                    const float kx = static_cast<float>(v->pixels->width()) / tally_size.width, ky = static_cast<float>(v->pixels->height()) / tally_size.height;
                    const Rect src{b.x * kx, b.y * ky, b.width * kx, b.height * ky};
                    const Rect dst{d.x + b.x * s.shown_scale, d.y + b.y * s.shown_scale, b.width * s.shown_scale, b.height * s.shown_scale};
                    list.draw_image(s.images[i], src, dst, render::Sampling::linear, static_cast<float>(on));
                    // Its frame and id, while it is new.
                    const double fresh = on * (1 - smooth(s.t, when + 0.6, when + 1.1));
                    if (fresh > 0) {
                        paint_ticks(list, {dst.x - 4, dst.y - 4, dst.width + 8, dst.height + 8}, hex(ink::cyan, static_cast<float>(fresh)), 8, 1.5f, 0.5f);
                        const auto& tag = s.labels.get(p.services.fonts(), std::format("#{}", id.substr(id.rfind('/') == std::string::npos ? 0 : id.rfind('/') + 1)),
                                                       12, hex(ink::cyan, static_cast<float>(fresh)), 500, "JetBrains Mono");
                        tag.paint(list, {dst.x + dst.width + 8, dst.y + 2});
                    }
                }
            } else {
                list.draw_image(s.images[i], img_rect(i), d, render::Sampling::linear);
            }
        }
        paint_ticks(list, {d.x - 10, d.y - 10, d.width + 20, d.height + 20}, hex(ink::cyan, 0.6f), 16, 1.5f, 0);
        const auto& cap = s.labels.get(p.services.fonts(), std::format("runs in {} · 360 × 240", language_names[i]), 13, hex(ink::muted), 500, "JetBrains Mono");
        cap.paint(list, {d.x, d.y - 32});
    } else {
        // All four, side by side: the same pixels.
        const double u = smooth(s.t, s.switched, s.switched + 0.5);
        const float w = d.width * 0.48f, h = d.height * 0.48f;
        for (std::size_t i = 0; i < 4; ++i) {
            const Rect to{d.x + (i % 2) * (d.width - w), d.y + (i / 2) * (d.height - h), w, h};
            const Rect r{static_cast<float>(mix(d.x, to.x, u)), static_cast<float>(mix(d.y, to.y, u)), static_cast<float>(mix(d.width, to.width, u)),
                         static_cast<float>(mix(d.height, to.height, u))};
            list.fill_round_rect(r, CornerRadii::all(6), render::Paint::solid(hex(ink::paper)));
            if (s.tallies[i] && s.tallies[i]->pixels) list.draw_image(s.images[i], img_rect(i), r, render::Sampling::linear);
            paint_ticks(list, {r.x - 5, r.y - 5, r.width + 10, r.height + 10}, hex(ink::cyan, 0.6f), 10, 1.5f, 0);
            const auto& cap = s.labels.get(p.services.fonts(), std::string(language_names[i]), 13, hex(ink::cyan, static_cast<float>(u)), 600, "JetBrains Mono");
            cap.paint(list, {r.x + 6, r.y - 24});
        }
    }
}

}  // namespace

std::unique_ptr<Shot> code_shot(const ShotContext& c) { return std::make_unique<Code>(c); }

}  // namespace omui::promo
