module viewer;

import std;
import openmui;
import openmui.ui.markdown;
import mcpplibs.os;

namespace viewer {

namespace {

namespace md = omui::md;

// A name's text in UTF-8 and back, never through the code page (lint `paths`).
std::filesystem::path from_utf8(std::string_view s) { return std::filesystem::path(std::u8string(s.begin(), s.end())); }
std::string generic_utf8(const std::filesystem::path& p) {
    const std::u8string u = p.generic_u8string();
    return std::string(u.begin(), u.end());
}

std::filesystem::path index_of(const std::filesystem::path& dir) {
    for (const char* name : {"index.md", "README.md", "readme.md"})
        if (std::filesystem::exists(dir / name)) return dir / name;
    return dir / "index.md";
}

std::vector<Heading> headings_of(std::string_view source) {
    std::vector<Heading> out;
    const md::Document doc = md::parse(source);
    md::walk(doc.root, [&](const md::Block& b) {
        if (b.kind == md::Kind::heading) out.push_back({b.anchor, md::plain_text(b), b.level});
    });
    return out;
}

void open_in_browser(std::string_view address) {
    os::process::spawn_options o;
    if constexpr (os::is_windows) {
        o.program = "cmd";
        o.args = {"/c", "start", "", std::string(address)};
    } else if constexpr (os::is_macos) {
        o.program = "open";
        o.args = {std::string(address)};
    } else {
        o.program = "xdg-open";
        o.args = {std::string(address)};
    }
    o.out = o.err = os::process::stdio::null;
    (void)os::process::process::spawn(o);
}

omui::Runtime* runtime = nullptr;  // the app's (set up before the view mounts)

bool scroll_to(std::string_view anchor) {
    if (!runtime) return false;
    omui::Invocation call;
    call.args = omui::Value{}.set("anchor", std::string(anchor));
    return runtime->act(runtime->find("main/body/view/doc"), "scroll_to_anchor", call).exit_code() == 0;
}

// After opening a document: its heading exists once the new blocks are built,
// a frame or two later.
void scroll_when_shown(std::string anchor, int tries = 10) {
    if (!runtime || scroll_to(anchor) || tries == 0) return;
    runtime->after(omui::seconds(0), [anchor, tries] { scroll_when_shown(anchor, tries - 1); });
}

}  // namespace

bool Model::open(const std::filesystem::path& where, bool remember) {
    // Absolute first: on Windows a name like `\docs` is on the current drive.
    std::error_code ec;
    const std::filesystem::path place = std::filesystem::absolute(where, ec);
    const std::filesystem::path& w = ec ? where : place;
    const std::filesystem::path file = std::filesystem::is_directory(w) ? index_of(w) : w;
    auto text = os::fs::read(file);
    if (!text) {
        status.set(std::format("cannot open {}: {}", generic_utf8(file), text.error().message()));
        return false;
    }
    if (remember && !path.peek().empty()) back.push_back(path.peek());
    path.set(generic_utf8(std::filesystem::absolute(file)));
    seen = os::fs::stamp_of(file).value_or(os::fs::stamp{});
    toc.set(headings_of(*text));
    source.set(std::move(*text));
    status.set(std::format("{} headings", toc.peek().size()));
    return true;
}

void Model::reload_if_changed() {
    if (path.peek().empty()) return;
    const auto file = from_utf8(path.peek());
    const auto now = os::fs::stamp_of(file);
    if (!now || *now == seen) return;
    seen = *now;
    if (auto text = os::fs::read(file); text && *text != source.peek()) {
        toc.set(headings_of(*text));
        source.set(std::move(*text));
        status.set("reloaded");
    }
}

void Model::follow(std::string_view href) {
    if (href.starts_with('#')) return;  // the document scrolls to its own anchors
    if (href.find("://") != std::string_view::npos || href.starts_with("mailto:")) {
        (browse ? browse : open_in_browser)(href);
        status.set(std::format("opened {}", href));
        return;
    }
    // A document beside this one (and maybe a heading in it).
    const auto hash = href.find('#');
    const std::string_view rel = href.substr(0, hash);
    const std::string anchor = hash == std::string_view::npos ? std::string() : std::string(href.substr(hash + 1));
    const auto target = (from_utf8(path.peek()).parent_path() / from_utf8(rel)).lexically_normal();
    if (target.extension() == ".md" || std::filesystem::is_directory(target)) {
        if (open(target) && !anchor.empty() && runtime) runtime->after(omui::seconds(0), [anchor] { scroll_when_shown(anchor); });
        return;
    }
    (browse ? browse : open_in_browser)(generic_utf8(std::filesystem::absolute(target)));
}

void Model::watch(omui::Runtime& rt) {
    // The model outlives the runtime's timers: no shared ownership, no cycle.
    rt.after(omui::seconds(0.5), [this, &rt] {
        reload_if_changed();
        watch(rt);
    });
}

bool Model::go_back() {
    if (back.empty()) return false;
    const std::string previous = back.back();
    back.pop_back();
    return open(from_utf8(previous), false);
}

omui::AppSpec app(Model& m) {
    using namespace omui;
    AppSpec spec;
    spec.id = "org.openmui.examples.markdown-viewer";
    spec.title = "Markdown";
    spec.size = {980, 720};
    spec.setup = [&m](Runtime& rt) {
        runtime = &rt;
        m.watch(rt);  // a saved file shows at once
    };
    spec.view = [&m] {
        Element toc = scroll({.id = "toc", .width = 240, .gap = 2, .padding = Edges(12.0f), .background = "surface", .label = "Contents"},
                             {dynamic([&m] {
                                 std::vector<Element> out;
                                 const auto& all = m.toc.get();
                                 const std::string here = m.current.get();
                                 for (std::size_t i = 0; i < all.size(); ++i) {
                                     const Heading& h = all[i];
                                     const std::string anchor = h.anchor;
                                     Element b = button({.id = std::format("h{}", i), .text = h.text,
                                                         .variant = h.anchor == here ? ButtonVariant::secondary : ButtonVariant::ghost,
                                                         .on_activated = [anchor] { (void)scroll_to(anchor); },
                                                         .align_self = "start",
                                                         .margin = Edges(Insets{0, 0, 0, static_cast<float>(12 * (h.level - 1))})});
                                     b.key = std::format("{}:{}", i, h.anchor);
                                     out.push_back(std::move(b));
                                 }
                                 return out;
                             })});
        // The contents keep their width; the document takes what is left, however wide its widest line (it scrolls).
        toc.prop("shrink", Source(Value(0.0)));
        Element doc = scroll({.id = "view", .grow = 1, .padding = Edges(24.0f, 32.0f)},
                             {markdown({.id = "doc",
                                        .base_url = derive([&m] {
                                            const auto p = from_utf8(m.path.get());
                                            return generic_utf8(p.parent_path());
                                        }),
                                        .source = m.source,
                                        .on_heading_visible = [&m](std::string a) { m.current.set(std::move(a)); },
                                        .on_link_activated = [&m](std::string href) { m.follow(href); }})});
        doc.prop("min_width", Source(Value(0.0)));
        return column({.id = "app", .grow = 1, .background = "background"},
                      {
                          row({.id = "header", .padding = Edges(8.0f, 12.0f), .gap = 12, .align = "center", .background = "surface"},
                              {
                                  button({.id = "back",
                                          .disabled = derive([&m] { return m.path.get().empty() || m.back.empty(); }),
                                          .text = "Back", .variant = ButtonVariant::ghost, .on_activated = [&m] { (void)m.go_back(); }}),
                                  text({.id = "file", .text = derive([&m] { return omui::utf8(from_utf8(m.path.get()).filename()); }),
                                        .style = "label", .grow = 1}),
                                  text({.id = "status", .text = derive([&m] { return m.status.get(); }), .style = "caption"}),
                              }),
                          // min_height 0: the body may be shorter than the document, which scrolls inside it.
                          row({.id = "body", .min_height = 0, .align = "stretch", .grow = 1}, {toc, doc}),
                      });
    };
    return spec;
}

}  // namespace viewer
