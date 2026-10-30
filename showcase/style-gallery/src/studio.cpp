module openmui.showcase.style_gallery;

import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;

namespace omui::styles {

Studio::Studio() {
    if (auto r = themes::add_styles(); !r)
        for (const auto& d : r.error()) warn("styles: {}", d.text());
}

std::expected<void, std::string> Studio::show(std::string_view name, bool crossfade) {
    const auto st = theme::library().find_style(name);
    if (!st) return std::unexpected(std::format("no style is named `{}`", name));
    current_ = std::string(name);
    draft_ = *st;
    if (runtime)
        if (auto r = scope.empty() ? theme::apply_style(*runtime, name, crossfade) : theme::apply_style(*runtime, name, scope, crossfade); !r) return r;
    revision.set(revision.peek() + 1);
    return {};
}

// The draft back into the library (as its own file) and onto the runtime: an
// edit is seen at once, without a cross-fade.
std::expected<void, std::string> Studio::apply_draft() {
    const std::string file = std::format("{}.style.omui.toml", draft_.name);
    draft_.file = file;
    if (auto r = theme::library().add_styles(theme::write_style(draft_), file); !r) {
        std::string why;
        for (const auto& d : r.error()) why += d.text() + "; ";
        return std::unexpected(why);
    }
    if (runtime)
        if (auto r = scope.empty() ? theme::apply_style(*runtime, draft_.name, false) : theme::apply_style(*runtime, draft_.name, scope, false); !r) return r;
    revision.set(revision.peek() + 1);
    return {};
}

std::expected<void, std::string> Studio::set_color(std::string_view token, std::string_view hex) {
    const auto c = Color::parse(hex);
    if (!c) return std::unexpected(std::format("`{}` is not a colour (#rgb, #rrggbb, #rrggbbaa)", hex));
    draft_.tokens.colors[std::string(token)] = *c;
    return apply_draft();
}

std::expected<void, std::string> Studio::set_radius(std::string_view token, double value) {
    if (!(value >= 0 && value <= 64)) return std::unexpected("a radius is 0 to 64");
    draft_.tokens.radius[std::string(token)] = static_cast<float>(value);
    return apply_draft();
}

std::expected<void, std::string> Studio::set_param(std::string_view selector, std::string_view param, double value) {
    for (EffectRule& r : draft_.effects)
        if (r.selector == selector) {
            r.effect.set(param, Value(value));
            return apply_draft();
        }
    return std::unexpected(std::format("the style `{}` puts no effect on `{}`", draft_.name, selector));
}

std::expected<std::filesystem::path, std::string> Studio::export_style() {
    std::error_code ec;
    const auto dir = export_dir.empty() ? std::filesystem::current_path(ec) : export_dir;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / std::format("{}.style.omui.toml", draft_.name);
    std::ofstream out(file, std::ios::binary);
    if (!out) return std::unexpected(std::format("`{}` cannot be written", utf8(file)));
    out << theme::write_style(draft_);
    if (!out) return std::unexpected(std::format("`{}` was not written whole", utf8(file)));
    return file;
}

namespace {

// The wallpaper behind the panels: soft discs of the theme's colours — what
// glass bends and frosts, what neon glows over.
Element wallpaper() {
    return painter({.id = "wallpaper",
                    .width = Dim(layout::pct(100)),
                    .height = Dim(layout::pct(100)),
                    .role = Role::none,
                    .paint = [](PaintContext& p) {
                        const float w = p.size.width, h = p.size.height;
                        p.list.fill_rect({0, 0, w, h}, p.theme.color("background"));
                        struct Disc {
                            float x, y, r;
                            std::string_view token;
                            float a;
                        };
                        constexpr std::array<Disc, 5> discs{{{0.18f, 0.25f, 0.22f, "accent", 0.55f},
                                                             {0.78f, 0.2f, 0.18f, "danger", 0.35f},
                                                             {0.62f, 0.78f, 0.26f, "success", 0.3f},
                                                             {0.3f, 0.85f, 0.16f, "warning", 0.35f},
                                                             {0.92f, 0.62f, 0.14f, "accent_subtle", 0.6f}}};
                        for (const Disc& d : discs) {
                            render::Path path;
                            path.add_circle({d.x * w, d.y * h}, d.r * std::min(w, h) * 0.9f);
                            p.list.fill_path(path, render::FillRule::nonzero, render::Paint::solid(p.theme.color(d.token).with_alpha(d.a)));
                        }
                    }});
}

// A panel: a region, so the style's "role:region" effect is its.
Element panel(std::string id, std::string title, std::vector<Element> children, float grow = 0) {
    std::vector<Element> all{text({.text = std::move(title), .style = "heading"})};
    for (Element& c : children) all.push_back(std::move(c));
    Element e = column({.id = std::move(id), .min_width = 0, .padding = 18, .gap = 12, .grow = grow, .background = "surface", .border_color = "border",
                        .border_width = 1, .radius = 16},
                       std::move(all));
    e.prop("role", Source(Value("region")));
    return e;
}

Element stat(std::string id, std::string label, std::string value, double share) {
    return panel(std::move(id), std::move(label),
                 {text({.text = std::move(value), .style = "title"}), progress({.label = "Of the goal", .value = share, .width = Dim(layout::pct(100))})}, 1);
}

Element preview(Studio& s) {
    Element settings = panel("settings", "Settings",
                             {text_box({.id = "name", .label = "Display name", .text = s.name, .on_changed = [&s](std::string v) { s.name.set(std::move(v)); }}),
                              toggle({.id = "notifications", .checked = s.notifications, .label = "Notifications",
                                      .on_changed = [&s](bool v) { s.notifications.set(v); }}),
                              toggle({.id = "cellular", .checked = s.cellular, .label = "Sync over cellular", .on_changed = [&s](bool v) { s.cellular.set(v); }}),
                              checkbox({.id = "backup", .checked = s.backup, .label = "Back up every night", .on_changed = [&s](bool v) { s.backup.set(v); }}),
                              slider({.id = "volume", .label = "Volume", .max = 1, .min = 0, .step = 0.05, .value = s.volume,
                                      .on_changed = [&s](double v) { s.volume.set(v); }}),
                              select({.id = "region", .label = "Region", .labels = std::vector<std::string>{"Europe", "Americas", "Asia"},
                                      .options = std::vector<std::string>{"eu", "am", "as"}, .value = s.region,
                                      .on_changed = [&s](std::string v) { s.region.set(std::move(v)); }}),
                              row({.gap = 10, .justify = "end"},
                                  {button({.id = "cancel", .text = "Cancel", .variant = ButtonVariant::secondary}),
                                   button({.id = "save", .text = "Save", .variant = ButtonVariant::primary,
                                           .on_activated = [&s] { s.saved.set(s.saved.peek() + 1); }})})});
    Element dashboard = column({.id = "dashboard", .min_width = 380, .gap = 16, .grow = 1},
                               {row({.gap = 16}, {stat("users", "Active users", "12,408", 0.72), stat("revenue", "Revenue", "$84.2k", 0.54)}),
                                panel("activity", "This week",
                                      {painter({.id = "chart",
                                                .width = Dim(layout::pct(100)),
                                                .height = 120,
                                                .role = Role::figure,
                                                .label = "Sign-ups by day",
                                                .paint = [](PaintContext& p) {
                                                    constexpr std::array<float, 7> days{0.42f, 0.65f, 0.51f, 0.83f, 0.74f, 0.92f, 0.6f};
                                                    const float w = p.size.width, h = p.size.height, gap = 10;
                                                    const float bar = (w - gap * 6) / 7;
                                                    for (std::size_t i = 0; i < days.size(); ++i) {
                                                        const float bh = days[i] * (h - 4);
                                                        p.list.fill_round_rect({static_cast<float>(i) * (bar + gap), h - bh, bar, bh}, CornerRadii::all(4),
                                                                               render::Paint::solid(p.theme.color(i == 5 ? "accent" : "accent_subtle")));
                                                    }
                                                }}),
                                       text({.text = "Saturday was the best day yet.", .style = "caption", .color = "text_muted"})})});
    // The panels scroll over the wallpaper; where the preview is narrow (the
    // workshop inside a page) the dashboard goes below the settings. A basis
    // of 0: a line breaks by its least width, not its content's.
    dashboard.prop("basis", Source(Value(0.0)));
    return stack({.id = "preview", .min_width = 0, .align = "start", .grow = 1, .clip = true},
                 {wallpaper(), scroll({.id = "content", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))},
                                      {row({.id = "panels", .width = Dim(layout::pct(100)), .padding = 32, .gap = 24, .wrap = "wrap"},
                                           {column({.width = 380}, {std::move(settings)}), std::move(dashboard)})})});
}

// The sidebar: the packs, and the export.
Element sidebar(Studio& s) {
    return immediate(
        [&s](Ui& ui) {
            (void)s.revision.get();  // rebuilt when the style changes
            ui.text("Styles", {.style = "title"});
            for (std::string_view n : themes::style_names) {
                auto scope = ui.scope(n);
                const auto st = theme::library().find_style(n);
                ui.column({.id = std::format("pack-{}", n), .gap = 2}, [&] {
                    if (ui.button(n, {.id = std::format("show-{}", n), .variant = n == s.current() ? "primary" : "ghost"})) (void)s.show(n);
                    if (st) ui.text(st->doc, {.style = "caption", .color = Fill("text_muted")});
                });
            }
            ui.spacer();
            if (s.can_export && ui.button("Export", {.id = "export", .variant = "secondary", .verb = "styles.export"})) {}
        },
        {.id = "sidebar", .width = 220, .padding = 16, .gap = 10, .shrink = 0, .background = "surface", .border_color = "border", .border_width = 1});
}

// The editor: the shown pack's accent, its corners, its effects' parameters.
Element editor(Studio& s) {
    return immediate(
        [&s](Ui& ui) {
            (void)s.revision.get();
            const theme::Style& d = s.draft();
            ui.text(std::format("Editing {}", d.name), {.style = "heading"});
            std::string accent = [&] {
                const auto it = d.tokens.colors.find("accent");
                const Theme* shown = !s.runtime ? nullptr : s.scope.empty() ? &s.runtime->theme() : s.runtime->scope_theme(s.scope);
                const Color c = it != d.tokens.colors.end() ? it->second : shown ? shown->color("accent") : Color::black();
                auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
                return std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
            }();
            if (ui.text_box("Accent", accent, {.id = "accent"})) (void)s.set_color("accent", accent);
            double radius = [&] {
                const auto it = d.tokens.radius.find("medium");
                return it != d.tokens.radius.end() ? static_cast<double>(it->second) : 8.0;
            }();
            ui.text("Corners", {.style = "caption", .color = Fill("text_muted")});
            if (ui.slider("Corners", radius, {.id = "corners", .min = 0, .max = 32, .step = 1})) (void)s.set_radius("medium", radius);
            for (const EffectRule& r : d.effects) {
                auto scope = ui.scope(r.selector);
                const std::string effect(r.effect.get("name") ? r.effect.get("name")->as_string() : "");
                ui.text(std::format("{} on {}", effect, r.selector), {.style = "label"});
                if (const Value::Map* m = r.effect.map())
                    for (const auto& [param, value] : *m) {
                        if (!value.is_number()) continue;
                        const auto ref = render::effects().find(effect);
                        const render::EffectInfo* info = ref ? render::effects().info(*ref) : nullptr;
                        double lo = 0, hi = 1;
                        if (info)
                            for (const auto& ep : info->params)
                                if (ep.name == param) lo = ep.min, hi = std::min<double>(ep.max, std::max(ep.min + 1.0, static_cast<double>(ep.value) * 4));
                        double v = value.as_double();
                        ui.text(param, {.style = "caption", .color = Fill("text_muted")});
                        std::string id = std::format("{}-{}", r.selector, param);  // role:region → role-region-…: an id's characters
                        std::ranges::replace(id, ':', '-');
                        if (ui.slider(param, v, {.id = id, .min = lo, .max = std::max(hi, v), .step = (hi - lo) / 100}))
                            (void)s.set_param(r.selector, param, v);
                    }
            }
        },
        {.id = "editor", .width = 300, .padding = 16, .gap = 8, .shrink = 0, .background = "surface", .border_color = "border", .border_width = 1});
}

}  // namespace

AppSpec app(Studio& s, std::string_view first) {
    AppSpec a;
    a.id = "org.openmui.styles";
    a.title = "Style workshop";
    a.size = {1440, 900};
    a.setup = [&s, first = std::string(first)](Runtime& rt) {
        register_base_components();  // the immediate sidebar and editor ask for them by name
        s.runtime = &rt;
        if (auto r = s.show(first, false); !r) warn("styles: {}", r.error());
    };
    add_verbs(a, s);
    a.view = [&s] { return view(s); };
    return a;
}

Element view(Studio& s) {
    Element e = row({.id = "studio", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "stretch", .background = "background"},
                    {sidebar(s), preview(s), editor(s)});
    if (!s.scope.empty()) e.prop("theme_scope", Source(Value(s.scope)));
    return e;
}

void add_verbs(AppSpec& a, Studio& s) {
    a.verbs.push_back({{"styles.show", "Shows the interface in a style pack (a cross-fade from the last).", Effect::reversible, {}, {{"name", "string"}}},
                       [&s](const Value& args) -> VerbResult {
                           const Value* n = args.get("name");
                           if (!n) return std::unexpected(std::string("styles.show takes a name"));
                           if (auto r = s.show(n->as_string()); !r) return std::unexpected(r.error());
                           return Value(s.current());
                       }});
    if (!s.can_export) return;  // a page: nowhere to write
    a.verbs.push_back({{"styles.export", "Writes the edited style pack as a *.style.omui.toml.", Effect::external, "yes", {}},
                       [&s](const Value&) -> VerbResult {
                           auto r = s.export_style();
                           if (!r) return std::unexpected(r.error());
                           return Value(generic_utf8(*r));
                       }});
}

}  // namespace omui::styles
