// The application: the field filling the window, from level 1 on; the style
// pack a level asks for applied to the whole window (cross-faded); the field
// registered by name for view documents, scripts and agents.
module openmui.showcase.widget_breakout;

import std;
import openmui;
import openmui.theme;
import openmui.theme.styles;

namespace omui::breakout {

void register_components() { register_type(component_type<Field>()); }

AppSpec app() {
    AppSpec a;
    a.id = "org.openmui.breakout";
    a.title = "组件打砖块";
    a.size = {1280, 720};
    a.theme = Theme::dark_theme();
    auto runtime = std::make_shared<Runtime*>(nullptr);
    a.setup = [runtime](Runtime& rt) {
        *runtime = &rt;
        register_components();
        if (auto r = themes::add_styles(); !r) warn("breakout: the style packs did not load");
    };
    a.view = [runtime] {
        return std::move(field({.id = "field", .level = 1, .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))})
                             .on("style", [runtime](const Value& args) {
                                 Runtime* rt = *runtime;
                                 if (!rt) return;
                                 const std::string_view name = args.get("name") ? args.get("name")->as_string() : "";
                                 if (name.empty()) {  // the application's own
                                     rt->crossfade(seconds(0.45));
                                     rt->set_theme(Theme::dark_theme());
                                     rt->set_effect_rules({});
                                 } else if (auto r = theme::apply_style(*rt, name, true); !r) {
                                     warn("breakout: {}", r.error());
                                 }
                             }));
    };
    return a;
}

}  // namespace omui::breakout
