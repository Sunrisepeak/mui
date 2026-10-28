// The application: the field filling the window, from level 1 on; the style
// pack a level asks for applied to the whole window (cross-faded); level 5's
// agent playing along; level 6's workshop over the field (its gift scripts in
// real text boxes, applied by hot reload); the field registered by name for
// view documents, scripts and agents.
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
    std::shared_ptr<Workshop>         workshop;  // made when level 6 first begins
    Signal<bool>                      open = signal(false);
    std::vector<Signal<std::string>>  text, status;

    // The rules on the field from the next step (the field checks them again).
    void hand_over(const Rules& r) {
        Invocation call;
        call.actor = {"program", "workshop", {}};
        call.args = Value{}
                        .set("drop_one_in", r.drop_one_in)
                        .set("multi_balls", r.multi_balls)
                        .set("pierce_steps", r.pierce_steps)
                        .set("slow_percent", r.slow_percent)
                        .set("slow_steps", r.slow_steps)
                        .set("wide_half", r.wide_half)
                        .set("wide_steps", r.wide_steps);
        (void)runtime->act(field, "set_rules", call);
    }
    // The rules the field plays by now.
    [[nodiscard]] Rules current() const {
        const Field* f = runtime->component<Field>(field);
        return f && f->state() ? f->state()->rules : Rules{};
    }
    // Level 6 began: the scripts' rules, once they have loaded.
    void begin_workshop() {
        if (!workshop) {
            workshop = std::make_shared<Workshop>(*runtime);
            for (std::size_t i = 0; i < workshop->scripts().size(); ++i) {
                text[i].set(workshop->scripts()[i].shipped);
                status[i].set("loading…");
            }
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
            warn("breakout: the gift scripts: {}", r.error());
            return;
        }
        hand_over(*r);
        for (std::size_t i = 0; i < workshop->scripts().size(); ++i) status[i].set("loaded");
    }
    void apply(std::size_t i) {
        if (!workshop) return;
        status[i].set("reloading…");
        workshop->apply(i, text[i].peek(), current(), [this, i](Applied a) {
            if (!a.rules) {
                status[i].set(std::format("kept the rules: {}", a.rules.error()));
                return;
            }
            hand_over(*a.rules);
            status[i].set(std::format("reloaded in {:.0f} ms — from the next step", a.ms));
        });
    }
};

// The workshop: over the field's right side, while open.
Element workshop_view(const std::shared_ptr<Bench>& b) {
    std::vector<Element> cards;
    const std::array<std::array<std::string_view, 3>, 4> files{{{"wide.ts", "ts", "wide"}, {"pierce.ts", "ts", "pierce"}, {"slow.lua", "lua", "slow"}, {"multi.py", "py", "multi"}}};
    for (std::size_t i = 0; i < files.size(); ++i) {
        const auto [file, language, gift] = files[i];
        auto preset = [b, i](double value) {
            return [b, i, value] {
                b->text[i].set(Workshop::retune(b->text[i].peek(), value));
                b->apply(i);
            };
        };
        const std::array<double, 2> presets = gift == "wide"     ? std::array<double, 2>{80, 260}
                                              : gift == "pierce" ? std::array<double, 2>{2, 12}
                                              : gift == "multi"  ? std::array<double, 2>{1, 4}
                                                                 : std::array<double, 2>{25, 80};
        // A script: its language, its text in a real text box, Apply, as shipped, two presets of its tuned value, and what came of the last.
        cards.push_back(column(
            {.id = std::string(gift), .gap = 6.0f},
            {text({.text = std::format("{} · {}", file, language_name(language)), .style = "label"}),
             text_box({.id = std::format("script.{}", gift), .label = std::string(file), .multiline = true, .text = b->text[i],
                       .on_changed = [b, i](std::string v) { b->text[i].set(std::move(v)); }, .height = Dim(150.0f)}),
             row({.id = std::format("tools.{}", gift), .gap = 6.0f},
                 {button({.id = std::format("apply.{}", gift), .text = "Apply", .variant = ButtonVariant::primary, .on_activated = [b, i] { b->apply(i); }}),
                  button({.id = std::format("undo.{}", gift), .text = "As shipped", .variant = ButtonVariant::ghost,
                          .on_activated = [b, i] {
                              if (!b->workshop) return;
                              b->text[i].set(b->workshop->scripts()[i].shipped);
                              b->apply(i);
                          }}),
                  button({.id = std::format("preset.{}.a", gift), .text = std::format("= {}", presets[0]), .variant = ButtonVariant::ghost, .on_activated = preset(presets[0])}),
                  button({.id = std::format("preset.{}.b", gift), .text = std::format("= {}", presets[1]), .variant = ButtonVariant::ghost, .on_activated = preset(presets[1])})}),
             text({.id = std::format("status.{}", gift), .text = b->status[i], .style = "caption"})}));
    }
    return column({.id = "workshop", .width = Dim(940.0f), .height = Dim(layout::pct(100)), .padding = 18, .gap = 10.0f, .align_self = "center",
                   .background = Color::hex(0x0b0e17, 0.94f), .visible = b->open},
                  {text({.text = "工坊 · Workshop", .style = "title"}),
                   text({.text = "The gifts are scripts in three languages. Edit the value marked ← tune and Apply: the script reloads and its rules hold from the "
                                 "next step.",
                         .style = "caption"}),
                   grid({.id = "scripts", .gap = 14.0f}, "1fr 1fr", std::move(cards)),
                   button({.id = "workshop.close", .text = "Play on (E)", .on_activated = [b] {
                               Invocation call;
                               (void)b->runtime->act(b->field, "workshop", call);
                           }})});
}

}  // namespace

void register_components() { register_type(component_type<Field>()); }

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.breakout";
    a.title = "组件打砖块";
    a.size = {1280, 720};
    a.theme = Theme::dark_theme();
    auto b = std::make_shared<Bench>();
    for (int i = 0; i < 4; ++i) b->text.push_back(signal(std::string())), b->status.push_back(signal(std::string()));
    a.setup = [b](Runtime& rt) {
        b->runtime = &rt;
        register_components();
        register_canvas_component();  // level 3's wall
        if (auto r = themes::add_styles(); !r) warn("breakout: the style packs did not load");
    };
    a.view = [b] {
        Element game = field({.id = "field", .level = 1, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))});
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
        game.on("level_began", [b](const Value& args) {
            Runtime* rt = b->runtime;
            const Value* n = args.get("number");
            if (!rt || !n) return;
            b->field = rt->find(args.get("path") ? args.get("path")->as_string() : "");
            const LevelDef& l = level(static_cast<std::int32_t>(n->as_int()));
            if (l.second_paddle) b->copilot->start(*rt, b->field);
            else b->copilot->stop();
            if (l.workshop) b->begin_workshop();
        });
        game.on("workshop", [b](const Value& args) { b->open.set(args.get("open") && args.get("open")->as_bool()); });
        return stack({.id = "game", .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}, {std::move(game), workshop_view(b)});
    };
    return a;
}

}  // namespace omui::breakout
