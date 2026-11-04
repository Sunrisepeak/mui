// The composition catalogue is a set of running, styled tools, not labels.
import std;
import openmui;
import openmui.site;
import openmui.theme;
import openmui.theme.styles;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {

struct Scene {
    site::MotionSceneState state;
    Signal<std::string> language = signal(std::string("en"));
    int changes = 0;
    const site::MotionCaseInfo& info;
    float width, height;
    bool clipboard;
    Headless h;
    Scene(const site::MotionCaseInfo& entry, float room, std::string_view lang = "en", float scene_height = 360, bool has_clipboard = true)
        : info(entry), width(room), height(scene_height), clipboard(has_clipboard), h(HeadlessOptions{.size = {room + 32, scene_height + 32}}) {
        language.set(std::string(lang));
        h.runtime().set_device_tier(pipeline::Tier::t4, "CPU reference for the full style effects");
        check(theme::apply_style(h.runtime(), info.style, "creative", false).has_value(), std::string(info.id));
        h.mount("main", column({.width = Dim(room + 32), .height = Dim(scene_height + 32), .padding = Edges(16), .background = "background"}, {
            dynamic([this] {
                site::MotionSceneContext context{&state, &language, width, height, [this] { ++changes; }};
                if (clipboard) context.clipboard = [this](std::string text) { h.runtime().set_clipboard_text(text); };
                Element e = site::creative_motion_scene(info.id, context);
                e.prop("theme_scope", Source(Value("creative")));
                e.prop("motion", Source(Value{}.set("use", std::string(info.use)).set("order", std::string(info.order)).set("stagger", info.stagger)
                    .set("rise", info.rise).set("grow", info.grow).set("stretch", info.stretch)));
                return std::vector<Element>{std::move(e)};
            })}));
        h.settle();
    }
    Value model() const {
        Value::List items; for (const auto& item : state.items.peek()) items.emplace_back(item);
        return Value{}.set("end", state.end.peek()).set("checked", state.checked.peek()).set("enabled", state.enabled.peek())
            .set("value", state.value.peek()).set("secondary", state.secondary.peek()).set("count", state.count.peek())
            .set("text", state.text.peek()).set("choice", state.choice.peek()).set("items", Value(std::move(items)));
    }
    std::uint64_t pixels() { return site::frame_hash(h.pixels()); }
    bool act(std::string_view node, std::string_view action, Value args = {}) {
        Invocation call; call.args = std::move(args);
        return h.tree().act("main/composition/" + std::string(node), action, call).exit_code() == std::optional<int>(0);
    }
    void inside() {
        for (const auto& node : h.tree().snapshot({.root = "main/composition"}).nodes) {
            if (!node.bounds || node.states.has(State::hidden)) continue;
            const bool control = node.role == Role::button || node.role == Role::checkbox || node.role == Role::switch_ || node.role == Role::slider ||
                node.role == Role::text_box || node.role == Role::combo_box || node.role == Role::spin_button || node.role == Role::tab || node.role == Role::radio;
            if (!control) continue;
            check(node.bounds->x >= 15.5f && node.bounds->right() <= width + 16.5f && node.bounds->y >= 15.5f && node.bounds->bottom() <= height + 16.5f,
                  std::format("{} {} {} at {}: {} ({},{} {}×{})", info.id, language.peek(), state.end.peek() ? "B" : "A", width,
                              node.id, node.bounds->x, node.bounds->y, node.bounds->width, node.bounds->height));
        }
    }
    bool effect(std::string_view name) {
        const auto ref = render::effects().find(name);
        for (const auto& command : h.runtime().display_list().commands())
            if (const auto* layer = std::get_if<render::PushLayer>(&command); layer && ref && layer->effect.id == ref->id) return true;
        return false;
    }
};

const site::MotionCaseInfo& entry(std::string_view id) {
    const auto all = site::creative_motions();
    return *std::ranges::find(all, id, &site::MotionCaseInfo::id);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"site.creative_motion"};
    (void)themes::add_styles();
    const auto media = scratch_dir("creative-motion");

    s.test("twenty distinct, named compositions have component, material and motion definitions", [] {
        const auto all = site::creative_motions();
        check(all.size() >= 20);
        std::set<std::string_view> ids, materials, groups;
        for (const auto& example : all) {
            check(ids.insert(example.id).second, std::string(example.id));
            check(!example.en.empty() && !example.zh.empty() && !example.purpose_en.empty() && !example.purpose_zh.empty(), std::string(example.id));
            check(!example.components.empty() && !example.use.empty(), std::string(example.id));
            check(example.style != "plain", std::string(example.id));
            materials.insert(example.style); groups.insert(example.group);
        }
        check(materials.size() >= 5 && groups.size() >= 6);
    });

    s.test("all compositions use actual styled controls and fit both endpoints on a phone and desktop in both languages", [media] {
        for (const auto& example : site::creative_motions()) for (const float width : {260.0f, 700.0f}) for (const auto language : {"en", "zh"}) {
            Scene a(example, width, language);
            a.inside();
            const auto before = a.pixels();
            check(a.h.save_png(media / std::format("{}-{}-{}-A.png", example.id, language, static_cast<int>(width))).has_value());
            a.state.end.set(true); a.h.advance(seconds(4)); a.h.settle(); a.inside();
            check(before != a.pixels(), std::format("{} has different endpoint drawings", example.id));
            check(a.h.save_png(media / std::format("{}-{}-{}-B.png", example.id, language, static_cast<int>(width))).has_value());
            const std::string_view effect = example.style == "glass" ? "liquid_glass" : example.style == "ink" ? "paper" : example.style == "retro" ? "crt" : example.style == "neon" ? "neon" : "metaball";
            check(a.effect(effect), std::format("{} actually draws {}", example.id, effect));
            if (width == 700 && std::string_view(language) == "en") {
                std::set<std::string_view> types;
                for (const auto& node : a.h.tree().snapshot({.root = "main/composition"}).nodes) {
                    const auto id = a.h.runtime().find(node.id); if (a.h.runtime().valid(id)) types.insert(a.h.runtime().type(id).name);
                }
                for (auto part : example.components | std::views::split('+')) {
                    std::string_view name(part.begin(), part.end());
                    while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
                    while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
                    check(types.contains(name), std::format("{} has an actual {}", example.id, name));
                }
            }
        }
    });

    s.test("each composition performs a real action and reports changed application state", [] {
        for (const auto& example : site::creative_motions()) {
            Scene a(example, 700); const auto before = a.model(); const auto pixels = a.pixels();
            if (example.id == "packing-board") check(a.h.click("main/composition/workspace/pack"));
            else if (example.id == "inbox-triage") check(a.act("workspace/file", "activate"));
            else if (example.id == "focus-session") check(a.act("session/begin", "activate"));
            else if (example.id == "release-steps") check(a.act("release/next", "activate"));
            else if (example.id == "price-comparison") check(a.act("workspace/billing", "set_value", Value{}.set("value", "yearly")));
            else if (example.id == "tag-sorting") {
                check(a.act("workspace/tags/topic-design", "remove")); a.h.settle();
                check(a.act("workspace/topic", "set_value", Value{}.set("value", "sketch"))); a.h.settle();
                check(a.h.click("main/composition/workspace/topic")); a.h.key(Key::enter);
            } else if (example.id == "playback-queue") check(a.act("player/transport/next", "activate"));
            else if (example.id == "palette-studio") check(a.act("workspace/mix", "set_value", Value{}.set("value", 75.0)));
            else if (example.id == "appointment-card") check(a.act("appointment/day", "set_value", Value{}.set("value", "2026-12-02")));
            else if (example.id == "reading-mode") check(a.act("workspace/focus", "activate"));
            else if (example.id == "transfer-center") check(a.act("workspace/transfer", "activate"));
            else if (example.id == "split-notes") check(a.act("workspace/panes/editor/draft", "set_value", Value{}.set("value", "Keep the idea")));
            else if (example.id == "team-presence") check(a.act("workspace/availability", "toggle"));
            else if (example.id == "volume-mixer") check(a.act("mixer/level", "set_value", Value{}.set("value", 65.0)));
            else if (example.id == "consent-receipt") check(a.act("choice/preference", "toggle"));
            else if (example.id == "search-command") check(a.act("terminal/run", "activate"));
            else if (example.id == "weather-plan") check(a.act("forecast/indoors", "toggle"));
            else if (example.id == "budget-allocation") check(a.act("budget/available", "set_range", Value{}.set("low", 25.0).set("high", 85.0)));
            else if (example.id == "workspace-tabs") check(a.act("workspace/save", "activate"));
            else if (example.id == "issue-resolution") check(a.act("record/resolve", "activate"));
            a.h.settle();
            check(a.model() != before && a.changes > 0, std::format("{} changed real data", example.id));
            check(a.pixels() != pixels, std::format("{} changed the drawing", example.id));
        }
    });

    s.test("palette contrast changes actual colours and exports current usable CSS within a 300px phone scene", [media] {
        for (const auto language : {"en", "zh"}) {
            Scene a(entry("palette-studio"), 260, language, 300); a.inside();
            const auto swatch = [](std::size_t i, std::string_view leaf) { return "main/composition/workspace/swatches/swatch-" + std::to_string(i) + "/" + std::string(leaf); };
            std::array<std::string, 4> before;
            std::array<std::vector<std::uint8_t>, 4> fills;
            for (std::size_t i = 0; i < before.size(); ++i) {
                before[i] = a.h.tree().node(swatch(i, "hex"))->name;
                check(picking::parse_hex(before[i]).has_value());
                const auto image = a.h.png(a.h.runtime().bounds(a.h.runtime().find(swatch(i, "colour")))); check(image.has_value()); if (image) fills[i] = *image;
            }
            check(a.act("workspace/mix", "set_value", Value{}.set("value", 85.0))); a.h.settle(); a.inside();
            std::set<std::string> distinct;
            for (std::size_t i = 0; i < before.size(); ++i) {
                const auto hex = a.h.tree().node(swatch(i, "hex"))->name; check(hex != before[i] && picking::parse_hex(hex).has_value()); distinct.insert(hex);
                const auto image = a.h.png(a.h.runtime().bounds(a.h.runtime().find(swatch(i, "colour")))); check(image.has_value() && *image != fills[i], "the colour patch itself changes, independently of its label");
            }
            check_eq(distinct.size(), std::size_t(4));
            check(a.act("workspace/ink", "set_value", Value{}.set("value", "#0d9488"))); a.h.settle();
            check(a.act("workspace/actions/copy", "activate"));
            const auto css = a.h.runtime().clipboard_text(); check(css.starts_with(":root {\n") && css.contains("--palette-light:") && css.contains("--palette-shade:"));
            for (std::size_t i = 0; i < before.size(); ++i) check(css.contains(a.h.tree().node(swatch(i, "hex"))->name));
            check(a.h.save_png(media / (std::string("palette-useful-") + language + "-300.png")).has_value());
            a.state.end.set(true); a.h.settle(); a.inside();
        }
        Scene missing(entry("palette-studio"), 260, "en", 300, false);
        check(missing.h.tree().node("main/composition/workspace/actions/copy")->states.has(State::disabled));
        check(!missing.act("workspace/actions/copy", "activate") && missing.h.runtime().clipboard_text().empty());
    });

    s.test("compositions can be sought at real intermediate frames with all active handles held", [media] {
        for (const auto id : {"packing-board", "inbox-triage", "team-presence", "palette-studio", "workspace-tabs"}) {
            Scene a(entry(id), 700); a.state.end.set(true); (void)a.h.step();
            const auto motions = a.h.runtime().motions();
            check(!motions.empty(), id);
            if (std::string_view(id) == "packing-board")
                check(std::ranges::any_of(motions, [](const auto& motion) { return motion.path == "main/composition"; }),
                    "the explicit parent detects its default-motion cards changing allocation during reflow");
            for (const auto& motion : motions) a.h.runtime().pause_motion(motion.node);
            std::set<std::uint64_t> drawings;
            for (int frame = 0; frame <= 16; ++frame) {
                for (const auto& motion : motions) if (const auto time = a.h.runtime().motion_time(motion.node)) a.h.runtime().seek_motion(motion.node, time->first * static_cast<double>(frame) / 16.0);
                (void)a.h.step(); a.h.record(); drawings.insert(a.pixels());
                if (std::string_view(id) == "packing-board") a.inside();
                if (frame == 8) check(a.h.save_png(media / (std::string(id) + "-midpoint.png")).has_value());
            }
            check(drawings.size() >= 3, std::format("{} has distinct intermediate drawings", id));
            check(a.h.save_recording(media / (std::string(id) + ".webp"), {.delay_num = 1, .delay_den = 12, .lossless = true}).has_value());
            a.h.advance(seconds(0.5));
            check(a.h.runtime().frame_demand().idle(), std::format("{} stops asking for frames while paused", id));
        }
    });

    return s.run(argc, argv);
}
