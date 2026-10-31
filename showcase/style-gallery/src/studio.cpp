module openmui.showcase.style_gallery;

import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;
import mcpplibs.os;

namespace omui::styles {

namespace {
constexpr double corner_max = 32;  // the Corners field's range, the slider's and styles.set's alike
}  // namespace

Studio::Studio() {
    if (auto r = themes::add_styles(); !r)
        for (const auto& d : r.error()) warn("styles: {}", d.text());
}

std::expected<void, std::string> Studio::show(std::string_view name, bool crossfade) {
    const auto st = theme::library().find_style(name);
    if (!st) return std::unexpected(std::format("no style is named `{}`", name));
    // The pack left keeps its history; the one shown takes its own back.
    if (!current_.empty()) kept_[current_] = {std::move(undo_), std::move(redo_)};
    current_ = std::string(name);
    // The pack as shipped: the first time it is shown (an edited one, shown
    // again, keeps its edits; reset goes back to this).
    static std::map<std::string, theme::Style, std::less<>> shipped;
    if (!shipped.contains(name)) shipped.emplace(std::string(name), *st);
    original_ = shipped.find(name)->second;
    draft_ = *st;
    undo_.clear();
    redo_.clear();
    if (auto k = kept_.find(name); k != kept_.end()) {
        undo_ = std::move(k->second.undo);
        redo_ = std::move(k->second.redo);
        kept_.erase(k);
    }
    last_key_.clear();
    problem.set(std::string());
    accent_text.set(std::string());
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

// One step of the history: the draft before, unless the same field changed
// in the same drag (one press of the pointer), or by typing or keys a moment
// ago: then it joins that step. Whole or not at all: a change the pack does
// not take leaves the draft and its history as they were.
std::expected<void, std::string> Studio::edit(std::string key, const std::function<std::expected<void, std::string>(theme::Style&)>& change) {
    theme::Style next = draft_;
    if (auto r = change(next); !r) {
        problem.set(r.error());
        return r;
    }
    const double now = runtime ? runtime->now().count() : 0;
    const std::optional<std::uint64_t> gesture = runtime ? runtime->pointer_gesture() : std::nullopt;
    const bool same = key == last_key_ && !undo_.empty() && (gesture ? gesture == last_gesture_ : !last_gesture_ && now - last_at_ < 1.0);
    theme::Style before = std::exchange(draft_, std::move(next));
    if (auto r = apply_draft(); !r) {
        draft_ = std::move(before);
        (void)apply_draft();
        problem.set(r.error());
        return r;
    }
    if (!same) undo_.push_back(std::move(before));
    redo_.clear();
    last_key_ = std::move(key);
    last_at_ = now;
    last_gesture_ = gesture;
    problem.set(std::string());
    return {};
}

std::expected<void, std::string> Studio::set_color(std::string_view token, std::string_view hex) {
    const auto c = Color::parse(hex);
    if (!c) {
        problem.set(std::format("“{}” is not a colour: write #rgb, #rrggbb or #rrggbbaa", hex));
        return std::unexpected(std::format("`{}` is not a colour (#rgb, #rrggbb, #rrggbbaa)", hex));
    }
    return edit(std::format("color:{}", token), [&](theme::Style& s) -> std::expected<void, std::string> {
        s.tokens.colors[std::string(token)] = *c;
        return {};
    });
}

std::expected<void, std::string> Studio::set_radius(std::string_view token, double value) {
    return edit(std::format("radius:{}", token), [&](theme::Style& s) -> std::expected<void, std::string> {
        if (!(value >= 0 && value <= corner_max)) return std::unexpected(std::format("corners are 0 to {} px, not {}", corner_max, value));
        s.tokens.radius[std::string(token)] = static_cast<float>(value);
        return {};
    });
}

std::expected<void, std::string> Studio::set_param(std::string_view selector, std::string_view param, double value) {
    return edit(std::format("{}/{}", selector, param), [&](theme::Style& s) -> std::expected<void, std::string> {
        for (EffectRule& r : s.effects)
            if (r.selector == selector) {
                const std::string effect(r.effect.get("name") ? r.effect.get("name")->as_string() : "");
                const auto ref = render::effects().find(effect);
                const render::EffectInfo* info = ref ? render::effects().info(*ref) : nullptr;
                if (!info) return std::unexpected(std::format("no effect `{}` is registered", effect));
                const auto ep = std::ranges::find(info->params, param, &render::EffectParam::name);
                if (ep == info->params.end()) {
                    std::string names;
                    for (const auto& p : info->params) names += (names.empty() ? "" : ", ") + std::string(p.name);
                    return std::unexpected(std::format("{} has no parameter `{}` (it has {})", effect, param, names));
                }
                if (!(value >= ep->min && value <= ep->max)) return std::unexpected(std::format("{} of {} is {} to {}, not {}", param, effect, ep->min, ep->max, value));
                r.effect.set(param, Value(value));
                return {};
            }
        return std::unexpected(std::format("the style `{}` puts no effect on `{}`", s.name, selector));
    });
}

std::expected<void, std::string> Studio::set(std::string_view what, const Value& value) {
    if (what == "accent") return set_color("accent", value.as_string());
    if (!value.is_number()) return std::unexpected(std::format("`{}` takes a number", what));
    if (what == "corners") return set_radius("medium", value.as_double());
    const auto slash = what.rfind('/');
    if (slash == std::string_view::npos) return std::unexpected(std::format("no field `{}` (accent, corners, <selector>/<param>)", what));
    return set_param(what.substr(0, slash), what.substr(slash + 1), value.as_double());
}

std::expected<void, std::string> Studio::undo() {
    if (undo_.empty()) return std::unexpected("nothing to undo");
    redo_.push_back(std::move(draft_));
    draft_ = std::move(undo_.back());
    undo_.pop_back();
    last_key_.clear();
    problem.set(std::string());
    accent_text.set(std::string());
    return apply_draft();
}

std::expected<void, std::string> Studio::redo() {
    if (redo_.empty()) return std::unexpected("nothing to redo");
    undo_.push_back(std::move(draft_));
    draft_ = std::move(redo_.back());
    redo_.pop_back();
    last_key_.clear();
    accent_text.set(std::string());
    return apply_draft();
}

std::expected<void, std::string> Studio::reset() {
    if (!edited()) return {};
    undo_.push_back(draft_);
    redo_.clear();
    last_key_.clear();
    draft_ = original_;
    problem.set(std::string());
    accent_text.set(std::string());
    return apply_draft();
}

bool Studio::edited() const noexcept { return theme::write_style(draft_) != theme::write_style(original_); }

void Studio::save_settings() {
    saved_ = {notifications.peek(), cellular.peek(), backup.peek(), volume.peek(), name.peek(), region.peek()};
    saved.set(saved.peek() + 1);
    status.set("Saved.");
}

void Studio::cancel_settings() {
    const bool any = settings_changed();
    notifications.set(saved_.notifications);
    cellular.set(saved_.cellular);
    backup.set(saved_.backup);
    volume.set(saved_.volume);
    name.set(saved_.name);
    region.set(saved_.region);
    status.set(any ? "Changes discarded." : "Nothing to discard.");
}

bool Studio::settings_changed() const {
    (void)saved.get();  // a save moves what "changed" is measured from: whoever asks hears of it
    return notifications.get() != saved_.notifications || cellular.get() != saved_.cellular || backup.get() != saved_.backup ||
           volume.get() != saved_.volume || name.get() != saved_.name || region.get() != saved_.region;
}

std::expected<std::filesystem::path, std::string> Studio::export_style() {
    std::error_code ec;
    const auto dir = export_dir.empty() ? std::filesystem::current_path(ec) : export_dir;
    std::filesystem::create_directories(dir, ec);
    const auto file = dir / std::format("{}.style.omui.toml", draft_.name);
    // Whole or not at all (a reader never sees half a file).
    if (auto w = mcpplibs::os::fs::write_atomic(file, theme::write_style(draft_)); !w)
        return std::unexpected(std::format("`{}` was not written: {}", utf8(file), w.error().message()));
    status.set(std::format("Exported {}.", utf8(file.filename())));
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

// The panels' corners: twice the pack's medium corner (16 for 8), as the
// Corners field sets it.
float panel_radius(const Studio& s) {
    const auto& r = s.draft().tokens.radius;
    const auto it = r.find("medium");
    return 2 * (it != r.end() ? it->second : 8.0f);
}

// A panel: a region, so the style's "role:region" effect is its.
Element panel(Studio& s, std::string id, std::string title, std::vector<Element> children, float grow = 0) {
    const std::string title_of = title;
    std::vector<Element> all{text({.text = std::move(title), .style = "heading"})};
    for (Element& c : children) all.push_back(std::move(c));
    Element e = column({.id = std::move(id), .min_width = 0, .padding = 18, .gap = 12, .grow = grow, .background = "surface", .border_color = "border",
                        .border_width = 1, .radius = computed([&s] {
                            (void)s.revision.get();
                            return panel_radius(s);
                        })},
                       std::move(all));
    e.prop("role", Source(Value("region")));
    e.prop("label", Source(Value(title_of)));  // a region is named (S-6): its heading
    return e;
}

Element stat(Studio& s, std::string id, std::string label, std::string value, double share) {
    return panel(s, std::move(id), std::move(label),
                 {text({.text = std::move(value), .style = "title"}), progress({.label = "Of the goal", .value = share, .width = Dim(layout::pct(100))}),
                  text({.text = std::format("{:.0f}% of the month's goal", share * 100), .style = "caption", .color = "text_muted"})},
                 1);
}

Element preview(Studio& s) {
    Element settings = panel(s, "settings", "Settings",
                             {text({.text = "Display name", .style = "label"}),
                              text_box({.id = "name", .label = "Display name", .text = s.name, .on_changed = [&s](std::string v) { s.name.set(std::move(v)); }}),
                              toggle({.id = "notifications", .checked = s.notifications, .label = "Notifications",
                                      .on_changed = [&s](bool v) { s.notifications.set(v); }}),
                              toggle({.id = "cellular", .checked = s.cellular, .label = "Sync over cellular", .on_changed = [&s](bool v) { s.cellular.set(v); }}),
                              checkbox({.id = "backup", .checked = s.backup, .label = "Back up every night", .on_changed = [&s](bool v) { s.backup.set(v); }}),
                              row({.id = "volume_row", .gap = 8, .align = "center"},
                                  {text({.text = "Volume", .style = "label", .width = Dim(64.0f)}),
                                   slider({.id = "volume", .label = "Volume", .max = 1, .min = 0, .step = 0.05, .value = s.volume,
                                           .on_changed = [&s](double v) { s.volume.set(v); }, .grow = 1}),
                                   text({.text = computed([&s] { return std::format("{:.0f}%", s.volume.get() * 100); }), .style = "label",
                                         .align = "end", .width = Dim(44.0f)})}),
                              text({.text = "Region", .style = "label"}),
                              select({.id = "region", .label = "Region", .labels = std::vector<std::string>{"Europe", "Americas", "Asia"},
                                      .options = std::vector<std::string>{"eu", "am", "as"}, .value = s.region,
                                      .on_changed = [&s](std::string v) { s.region.set(std::move(v)); }}),
                              row({.id = "actions", .gap = 10, .align = "center"},
                                  {text({.id = "status",
                                         .text = computed([&s] {
                                             std::string said = s.status.get();
                                             return s.settings_changed() ? std::string("Unsaved changes") : said;
                                         }),
                                         .style = "caption",
                                         .color = "text_muted",
                                         .grow = 1}),
                                   button({.id = "cancel", .disabled = computed([&s] { return !s.settings_changed(); }), .text = "Cancel",
                                           .variant = ButtonVariant::secondary, .on_activated = [&s] { s.cancel_settings(); }}),
                                   button({.id = "save", .disabled = computed([&s] { return !s.settings_changed(); }), .text = "Save",
                                           .variant = ButtonVariant::primary, .on_activated = [&s] { s.save_settings(); }})})});
    Element dashboard = column({.id = "dashboard", .min_width = 380, .gap = 16, .grow = 1},
                               {row({.gap = 16}, {stat(s, "users", "Active users", "12,408", 0.72), stat(s, "revenue", "Revenue", "$84.2k", 0.54)}),
                                panel(s, "activity", "This week",
                                      {painter({.id = "chart",
                                                .width = Dim(layout::pct(100)),
                                                .height = 120,
                                                .role = Role::figure,
                                                .label = "Sign-ups by day: Mon 210, Tue 325, Wed 255, Thu 415, Fri 370, Sat 460, Sun 300",
                                                .paint = [](PaintContext& p) {
                                                    constexpr std::array<float, 7> days{0.42f, 0.65f, 0.51f, 0.83f, 0.74f, 0.92f, 0.6f};
                                                    // The best day the accent; the rest the accent half-strength, which still stands off the panel (3:1 and more).
                                                    const float w = p.size.width, h = p.size.height, gap = 10;
                                                    const float bar = (w - gap * 6) / 7;
                                                    for (std::size_t i = 0; i < days.size(); ++i) {
                                                        const float bh = days[i] * (h - 4);
                                                        p.list.fill_round_rect({static_cast<float>(i) * (bar + gap), h - bh, bar, bh}, CornerRadii::all(4),
                                                                               render::Paint::solid(p.theme.color("accent").with_alpha(i == 5 ? 1.0f : 0.62f)));
                                                    }
                                                }}),
                                       row({.id = "days", .justify = "space_between"},
                                           [] {
                                               // Each day's name and its number, under its bar.
                                               std::vector<Element> d;
                                               constexpr std::array<std::pair<std::string_view, int>, 7> week{
                                                   {{"Mon", 210}, {"Tue", 325}, {"Wed", 255}, {"Thu", 415}, {"Fri", 370}, {"Sat", 460}, {"Sun", 300}}};
                                               for (auto [day, n] : week)
                                                   d.push_back(column({.align = "center"}, {text({.text = std::to_string(n), .style = "label"}),
                                                                                           text({.text = std::string(day), .style = "caption", .color = "text_muted"})}));
                                               return d;
                                           }()),
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

// A parameter's name as a person reads it ("sweep_width" → "Sweep width"),
// and its value with the unit its description gives.
std::string human(std::string_view name) {
    // A colour's channels by name: tint_r → "Tint · red".
    for (auto [suffix, channel] : {std::pair{"_r", "red"}, {"_g", "green"}, {"_b", "blue"}, {"_a", "opacity"}})
        if (name.size() > 2 && name.ends_with(suffix)) return std::format("{} · {}", human(name.substr(0, name.size() - 2)), channel);
    std::string out(name);
    std::ranges::replace(out, '_', ' ');
    if (!out.empty() && out[0] >= 'a' && out[0] <= 'z') out[0] = static_cast<char>(out[0] - 'a' + 'A');
    return out;
}

std::string with_unit(double v, const render::EffectParam* ep) {
    const std::string_view doc = ep ? ep->doc : std::string_view{};
    if (doc.contains("logical pixels")) return std::format("{:.1f} px", v);
    if (doc.contains("degrees")) return std::format("{:.0f}°", v);
    if (ep && ep->min >= 0 && ep->max <= 1) return std::format("{:.0f}%", v * 100);
    return std::format("{:.2f}", v);
}

// The editor: the history (undo, redo, reset), the shown pack's accent, its
// corners, its effects' parameters — each with its name, its value and its
// unit, over a range that stays put while it is dragged — scrolling, as a
// pack of several effects has more of them than a window is tall.
Element editor(Studio& s, bool wide = false) {
    Element fields = immediate(
        [&s](Ui& ui) {
            (void)s.revision.get();
            (void)s.problem.get();
            const theme::Style& d = s.draft();
            ui.text(std::format("Editing {}", d.name), {.style = "heading"});
            ui.row({.id = "history", .gap = 6}, [&] {
                if (ui.button("Undo", {.id = "undo", .variant = "secondary", .disabled = !s.can_undo()})) (void)s.undo();
                if (ui.button("Redo", {.id = "redo", .variant = "ghost", .disabled = !s.can_redo()})) (void)s.redo();
                if (ui.button("Reset", {.id = "reset", .variant = "ghost", .disabled = !s.edited()})) (void)s.reset();
            });
            const std::string shown_accent = [&] {
                const auto it = d.tokens.colors.find("accent");
                const Theme* shown = !s.runtime ? nullptr : s.scope.empty() ? &s.runtime->theme() : s.runtime->scope_theme(s.scope);
                const Color c = it != d.tokens.colors.end() ? it->second : shown ? shown->color("accent") : Color::black();
                auto b = [](float v) { return static_cast<int>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255)); };
                return std::format("#{:02x}{:02x}{:02x}", b(c.r), b(c.g), b(c.b));
            }();
            // What was typed stays while it is not a colour, with why.
            std::string accent = s.accent_text.peek().empty() ? shown_accent : s.accent_text.peek();
            ui.text("Accent", {.style = "caption", .color = Fill("text_muted")});
            if (ui.text_box("Accent", accent, {.id = "accent", .placeholder = "#rrggbb"})) {
                s.accent_text.set(accent);
                if (s.set_color("accent", accent)) s.accent_text.set(std::string());
            }
            if (!s.problem.peek().empty()) ui.text(s.problem.peek(), {.id = "problem", .style = "caption", .color = Fill("danger")});
            double radius = [&] {
                const auto it = d.tokens.radius.find("medium");
                return it != d.tokens.radius.end() ? static_cast<double>(it->second) : 8.0;
            }();
            ui.text(std::format("Corners · {:.0f} px", radius), {.style = "caption", .color = Fill("text_muted")});
            if (ui.slider("Corners", radius, {.id = "corners", .min = 0, .max = corner_max, .step = 1})) (void)s.set_radius("medium", radius);
            struct Changed {
                std::string selector, param;
                double      value;
            };
            std::optional<Changed> changed;
            for (const EffectRule& r : d.effects) {
                auto scope = ui.scope(r.selector);
                const std::string effect(r.effect.get("name") ? r.effect.get("name")->as_string() : "");
                // Where it lies, as a person names it: a region is a panel here.
                const std::string_view what = r.selector.starts_with("role:") ? std::string_view(r.selector).substr(5) : std::string_view(r.selector);
                const std::string on = what == "region" ? std::string("the panels") : std::format("every {}", human(what));
                ui.text(std::format("{} — on {}", human(effect), on), {.style = "label"});
                const auto ref = render::effects().find(effect);
                const render::EffectInfo* info = ref ? render::effects().info(*ref) : nullptr;
                // The pack as shipped fixes each range: dragging never moves its ends.
                const Value* shipped = nullptr;
                for (const EffectRule& o : s.original().effects)
                    if (o.selector == r.selector) shipped = &o.effect;
                if (const Value::Map* m = r.effect.map())
                    for (const auto& [param, value] : *m) {
                        if (!value.is_number()) continue;
                        const render::EffectParam* ep = nullptr;
                        if (info)
                            for (const auto& p : info->params)
                                if (p.name == param) ep = &p;
                        const double was = shipped && shipped->get(param) ? shipped->get(param)->as_double() : value.as_double();
                        double lo = ep ? ep->min : 0, hi = ep ? ep->max : 1;
                        if (ep && hi - lo > 4) hi = std::min<double>(ep->max, std::max({lo + 1.0, 4.0 * ep->value, 2.0 * was}));
                        double v = std::clamp(value.as_double(), lo, hi);
                        ui.text(std::format("{} · {}", human(param), with_unit(v, ep)), {.style = "caption", .color = Fill("text_muted")});
                        std::string id = std::format("{}-{}", r.selector, param);  // role:region → role-region-…: an id's characters
                        std::ranges::replace(id, ':', '-');
                        if (ui.slider(human(param), v, {.id = id, .min = lo, .max = hi, .step = (hi - lo) / 100}))
                            changed = {r.selector, param, v};  // applied after the loop: an edit replaces the draft being read
                    }
            }
            if (changed) (void)s.set_param(changed->selector, changed->param, changed->value);
            ui.row({.id = "variants", .gap = 6}, [&] {
                bool clear = s.runtime && s.runtime->reduce_transparency();
                if (ui.toggle("Reduce transparency", clear, {.id = "reduce_transparency"}) && s.runtime) s.runtime->set_reduce_transparency(clear);
            });
        },
        {.id = "fields", .padding = 16, .gap = 8});
    return box({.id = "editor", .width = wide ? Dim(layout::pct(100)) : Dim(300.0f), .shrink = 0, .background = "surface", .border_color = "border", .border_width = 1},
               {scroll({.id = "scroll", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(fields)})});
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

// The packs as a row of buttons (a narrow workshop's chooser).
Element pack_row(Studio& s) {
    return immediate(
        [&s](Ui& ui) {
            (void)s.revision.get();
            ui.row({.id = "packs", .gap = 6, .wrap = "wrap"}, [&] {
                for (std::string_view n : themes::style_names)
                    if (ui.button(n, {.id = std::format("show-{}", n), .variant = n == s.current() ? "primary" : "ghost"})) (void)s.show(n);
            });
        },
        {.id = "chooser", .padding = 10, .background = "surface"});
}

Element view(Studio& s, bool narrow) {
    Element e = narrow ? scroll({.id = "studio", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .background = Fill("background")},
                                {pack_row(s), box({.id = "show", .width = Dim(layout::pct(100)), .height = Dim(760.0f)}, {preview(s)}),
                                 box({.id = "edit", .width = Dim(layout::pct(100)), .height = Dim(620.0f)}, {editor(s, true)})})
                       : row({.id = "studio", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .align = "stretch", .background = "background"},
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
    a.verbs.push_back({{"styles.set",
                        "Edits the pack shown: `what` is accent (a colour's text), corners (px), or <selector>/<param> (a number in the "
                        "parameter's range); a value refused changes nothing and says why.",
                        Effect::reversible, {}, {{"what", "string"}, {"value", "any"}}},
                       [&s](const Value& args) -> VerbResult {
                           const Value* w = args.get("what");
                           const Value* v = args.get("value");
                           if (!w || !v) return std::unexpected(std::string("styles.set takes `what` and `value`"));
                           if (auto r = s.set(w->as_string(), *v); !r) return std::unexpected(r.error());
                           return Value(theme::write_style(s.draft()).size() > 0);
                       }});
    for (auto [name, doc] : {std::pair{"styles.undo", "Takes back the last edit of the pack shown."},
                             {"styles.redo", "Puts forward again the edit last taken back."},
                             {"styles.reset", "The pack shown as it was shipped (itself an edit: undo takes it back)."}})
        a.verbs.push_back({{name, doc, Effect::reversible, {}, {}}, [&s, n = std::string_view(name)](const Value&) -> VerbResult {
                               auto r = n == "styles.undo" ? s.undo() : n == "styles.redo" ? s.redo() : s.reset();
                               if (!r) return std::unexpected(r.error());
                               return Value(s.edited());
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
