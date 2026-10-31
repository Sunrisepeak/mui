// The application: the field filling the window, from the run's first room
// on; the style pack a room asks for applied to the whole window
// (cross-faded); room 5's copilot playing along; between rooms, the two
// contraptions offered as cards over the field — each with its two presets,
// and its casing to open: the script inside it (TypeScript, Lua or Python)
// in a real text box, applied by hot reload; the field registered by name
// for view documents, scripts and agents.
module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;
import openmui.ui.canvas;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

const char* language_name(std::string_view l) { return l == "ts" ? "TypeScript" : l == "lua" ? "Lua" : "Python (PocketPy)"; }

// What the application keeps between its setup and its view.
struct Bench {
    Runtime*                          runtime = nullptr;
    NodeId                            field{};
    std::shared_ptr<Copilot>          copilot = std::make_shared<Copilot>();
    std::shared_ptr<Workshop>         workshop;  // made when the first choice opens (until then the rules are the scripts' as shipped)
    Signal<bool>                      open = signal(false);           // the choice
    Signal<std::int32_t>              casing = signal(std::int32_t{-1});  // the card whose casing is open
    std::vector<Signal<std::string>>  text, status;                    // by script
    std::array<Gift, 2>               offered{Gift::none, Gift::none};
    std::array<Signal<std::string>, 2> name{signal(std::string()), signal(std::string())}, line{signal(std::string()), signal(std::string())},
        brief{signal(std::string()), signal(std::string())}, steady{signal(std::string()), signal(std::string())},
        state{signal(std::string()), signal(std::string())};
    Signal<std::string>               heading = signal(std::string()), stats = signal(std::string()), casing_title = signal(std::string()),
                                      casing_status = signal(std::string());

    [[nodiscard]] Field* the_field() const { return runtime ? runtime->component<Field>(field) : nullptr; }
    // The script a contraption's casing holds.
    [[nodiscard]] std::size_t script_of(Gift g) const {
        const Contraption* c = contraption(g);
        if (!workshop || !c) return 0;
        for (std::size_t i = 0; i < workshop->scripts().size(); ++i)
            if (workshop->scripts()[i].gift == c->script) return i;
        return 0;
    }
    // The rules on the field from the next step, and for every room after (the field checks them again).
    void hand_over(const Rules& r) {
        Invocation call;
        call.actor = {"program", "workshop", {}};
        call.args = Value{}
                        .set("drop_one_in", r.drop_one_in)
                        .set("magnet_catches", r.magnet_catches)
                        .set("multi_balls", r.multi_balls)
                        .set("pierce_steps", r.pierce_steps)
                        .set("slow_percent", r.slow_percent)
                        .set("slow_steps", r.slow_steps)
                        .set("wide_half", r.wide_half)
                        .set("wide_steps", r.wide_steps);
        (void)runtime->act(field, "set_rules", call);
    }
    // The rules last handed over (what an edit of one script leaves the others at).
    [[nodiscard]] Rules current() const {
        const Field* f = the_field();
        return f ? f->rules() : Rules{};
    }
    // The first choice: the scripts loaded, and their rules once they have.
    void begin_workshop() {
        if (workshop) return;
        workshop = std::make_shared<Workshop>(*runtime);
        for (std::size_t i = 0; i < workshop->scripts().size(); ++i) {
            text[i].set(workshop->scripts()[i].shipped);
            status[i].set("loading…");
        }
        settle_rules(0);
    }
    void settle_rules(int tries) {
        auto r = workshop->rules(current());
        if (!r && r.error() == "the scripts are still loading" && tries < 2400) {
            runtime->after(seconds(1.0 / 120), [this, tries] { settle_rules(tries + 1); });
            return;
        }
        if (!r) {
            warn("breakout: the contraptions' scripts: {}", r.error());
            return;
        }
        hand_over(*r);
        for (std::size_t i = 0; i < workshop->scripts().size(); ++i) status[i].set("loaded");
    }
    // A script's text applied: reloaded in place, its answer checked, the rules handed over.
    void apply(std::size_t i, std::function<void(const std::string&)> said = {}) {
        if (!workshop) return;
        status[i].set("reloading…");
        workshop->apply(i, text[i].peek(), current(), [this, i, said = std::move(said)](Applied a) {
            std::string s = a.rules ? std::format("reloaded in {:.0f} ms — in force from the next step", a.ms)
                                    : std::format("kept the rules: {}", a.rules.error());
            if (a.rules) hand_over(*a.rules);
            status[i].set(s);
            if (said) said(s);
        });
    }
    // The card's contraption at one of its presets: the script's marked value retuned, then applied.
    void preset(std::size_t card, std::size_t which) {
        const Contraption* c = contraption(offered[card]);
        if (!c || !workshop) return;
        const std::size_t i = script_of(c->gift);
        text[i].set(Workshop::retune(text[i].peek(), c->presets[which]));
        state[card].set(std::format("tuning: {}", c->preset_names[which]));
        apply(i, [this, card, c, which](const std::string& s) {
            state[card].set(s.starts_with("reloaded") ? std::format("set: {}", c->preset_names[which]) : s);
            if (casing.peek() == static_cast<std::int32_t>(card)) casing_status.set(s);
        });
    }
    void take(std::size_t card) {
        const Contraption* c = contraption(offered[card]);
        if (!c) return;
        Invocation call;
        call.actor = runtime->acting().value_or(Actor{"person", "player", {}});  // whoever clicked Take takes it (openas)
        call.args = Value{}.set("gift", std::string(c->script));
        (void)runtime->act(field, "choose", call);
    }
    void show_casing(std::int32_t card) {
        casing.set(card);
        if (card < 0) return;
        const Contraption* c = contraption(offered[static_cast<std::size_t>(card)]);
        if (!c || !workshop) return;
        const GiftScript& g = workshop->scripts()[script_of(c->gift)];
        casing_title.set(std::format("{} · {}", g.file, language_name(g.language)));
        casing_status.set("Edit the value marked ← tune, then Apply: the script reloads, and its rules hold from the next step.");
    }
    // The choice opened after a room: what it offers, and how the run stands.
    void choosing(std::int32_t after) {
        const Field* f = the_field();
        if (!f) return;
        begin_workshop();
        if (const auto o = f->offered()) offered = *o;
        for (std::size_t k = 0; k < 2; ++k) {
            const Contraption* c = contraption(offered[k]);
            name[k].set(c ? std::string(c->name) : std::string());
            line[k].set(c ? std::string(c->line) : std::string());
            brief[k].set(c ? std::string(c->preset_names[0]) : std::string());
            steady[k].set(c ? std::string(c->preset_names[1]) : std::string());
            state[k].set(c ? std::format("{}  ·  press {}", c->script, k + 1) : std::string());
        }
        const Run& r = f->run_so_far();
        heading.set(std::format("{} {} — shut down", room(after).name, room(after).title));
        stats.set(std::format("score {}   ·   longest chain {}   ·   {} ◆   ·   {} {} left", r.score, r.best, r.sparks, r.lives, r.lives == 1 ? "ball" : "balls"));
        casing.set(-1);
        open.set(true);
    }
};

// A contraption offered: its card.
Element card_view(const std::shared_ptr<Bench>& b, std::size_t k) {
    const std::string id = std::format("offer.{}", k);
    return column({.id = id, .width = Dim(420.0f), .padding = 20, .gap = 10.0f, .background = Color::hex(0x101626, 0.96f), .radius = 16.0f},
                  {text({.text = b->name[k], .style = "title"}),
                   text({.text = b->line[k], .style = "body"}),
                   row({.id = id + ".presets", .gap = 8.0f},
                       {button({.id = id + ".brief", .text = b->brief[k], .variant = ButtonVariant::secondary, .on_activated = [b, k] { b->preset(k, 0); }}),
                        button({.id = id + ".steady", .text = b->steady[k], .variant = ButtonVariant::secondary, .on_activated = [b, k] { b->preset(k, 1); }})}),
                   text({.id = id + ".state", .text = b->state[k], .style = "caption"}),
                   row({.id = id + ".tools", .gap = 8.0f},
                       {button({.id = id + ".take", .text = std::format("Take it  ({})", k + 1), .variant = ButtonVariant::primary, .on_activated = [b, k] { b->take(k); }}),
                        button({.id = id + ".open", .text = "Open the casing", .variant = ButtonVariant::ghost,
                                .on_activated = [b, k] { b->show_casing(b->casing.peek() == static_cast<std::int32_t>(k) ? -1 : static_cast<std::int32_t>(k)); }})})});
}

// The casing open: the script inside, editable, applied by hot reload.
Element casing_view(const std::shared_ptr<Bench>& b) {
    auto script = [b]() -> std::size_t {
        const std::int32_t c = b->casing.peek();
        return c < 0 ? 0 : b->script_of(b->offered[static_cast<std::size_t>(c)]);
    };
    // The text box shows the open casing's script (the scripts' texts, by the casing open).
    Computed<std::string> source = computed([b] {
        const std::int32_t c = b->casing.get();
        if (c < 0 || !b->workshop) return std::string();
        return b->text[b->script_of(b->offered[static_cast<std::size_t>(c)])].get();
    });
    return column({.id = "casing", .width = Dim(860.0f), .padding = 16, .gap = 8.0f, .background = Color::hex(0x0b0e17, 0.97f), .radius = 14.0f,
                   .visible = computed([b] { return b->casing.get() >= 0; })},
                  {text({.text = b->casing_title, .style = "label"}),
                   text_box({.id = "casing.script", .label = "The contraption's script", .multiline = true, .text = source,
                             .on_changed = [b, script](std::string v) { b->text[script()].set(std::move(v)); }, .height = Dim(170.0f)}),
                   row({.id = "casing.tools", .gap = 8.0f},
                       {button({.id = "casing.apply", .text = "Apply", .variant = ButtonVariant::primary,
                                .on_activated = [b, script] {
                                    b->casing_status.set("reloading…");
                                    b->apply(script(), [b](const std::string& s) { b->casing_status.set(s); });
                                }}),
                        button({.id = "casing.shipped", .text = "As shipped", .variant = ButtonVariant::ghost,
                                .on_activated = [b, script] {
                                    if (!b->workshop) return;
                                    b->text[script()].set(b->workshop->scripts()[script()].shipped);
                                    b->casing_status.set("reloading…");
                                    b->apply(script(), [b](const std::string& s) { b->casing_status.set(s); });
                                }}),
                        button({.id = "casing.close", .text = "Close", .variant = ButtonVariant::ghost, .on_activated = [b] { b->show_casing(-1); }})}),
                   text({.id = "casing.status", .text = b->casing_status, .style = "caption"})});
}

// Between rooms: over the field, the room's end and the two contraptions offered.
Element between_view(const std::shared_ptr<Bench>& b) {
    return column({.id = "between", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100)), .padding = 28, .gap = 16.0f, .justify = "center",
                   .align = "center", .background = Color::hex(0x05070d, 0.72f), .visible = b->open},
                  {text({.id = "between.heading", .text = b->heading, .style = "title"}),
                   text({.id = "between.stats", .text = b->stats, .style = "caption"}),
                   text({.text = "Take one into the rooms ahead — the other is gone.", .style = "body"}),
                   row({.id = "offers", .gap = 24.0f}, {card_view(b, 0), card_view(b, 1)}),
                   casing_view(b)});
}

}  // namespace

void register_components() { register_type(component_type<Field>()); }

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.breakout";
    a.title = "组件打砖块 · 闹鬼的设置";
    a.size = {1280, 720};
    a.theme = Theme::dark_theme();
    auto b = std::make_shared<Bench>();
    for (int i = 0; i < 5; ++i) b->text.push_back(signal(std::string())), b->status.push_back(signal(std::string()));
    a.setup = [b](Runtime& rt) {
        b->runtime = &rt;
        register_components();
        register_canvas_component();  // room 3's cloud
        if (auto r = themes::add_styles(); !r) warn("breakout: the style packs did not load");
    };
    a.view = [b] {
        Element game = field({.id = "field", .room = 1, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))});
        game.on("style", [b](const Value& args) {
            Runtime* rt = b->runtime;
            if (!rt) return;
            const std::string_view name = args.get("name") ? args.get("name")->as_string() : "";
            if (name.empty()) {  // the application's own
                rt->crossfade(seconds(0.45));
                rt->set_theme(Theme::dark_theme());
                rt->set_effect_rules({});
            } else if (auto r = theme::apply_style(*rt, name, true); !r) {
                warn("breakout: {}", r.error());
            }
        });
        game.on("room_began", [b](const Value& args) {
            Runtime* rt = b->runtime;
            const Value* n = args.get("number");
            if (!rt || !n) return;
            b->field = rt->find(args.get("path") ? args.get("path")->as_string() : "");
            b->open.set(false);
            if (room(static_cast<std::int32_t>(n->as_int())).second_paddle) b->copilot->start(*rt, b->field);
            else b->copilot->stop();
        });
        game.on("choosing", [b](const Value& args) {
            const bool open = args.get("open") && args.get("open")->as_bool();
            if (open) b->choosing(args.get("after") ? static_cast<std::int32_t>(args.get("after")->as_int()) : 1);
            else b->open.set(false), b->casing.set(-1);
        });
        return stack({.id = "game", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(game), between_view(b)});
    };
    return a;
}

}  // namespace omui::breakout
