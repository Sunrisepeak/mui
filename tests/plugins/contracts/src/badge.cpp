module acme.ui;

import std;
import openmui.core;
import openmui.view;

namespace acme::ui {

void Badge::paint(omui::PaintContext& ctx) const {
    if (props.count == 0) return;
    const omui::Color fill = props.tone == BadgeTone::good ? ctx.theme.color("success")
                           : props.tone == BadgeTone::bad  ? ctx.theme.color("danger")
                                                           : ctx.theme.color("text_muted");
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, omui::CornerRadii::all(10), omui::render::Paint::solid(fill));
}

}  // namespace acme::ui
