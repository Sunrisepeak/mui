export module acme.ui:badge;

import std;
import openmui.core;
import openmui.view;
import :badge_contract;

export namespace acme::ui {

class Badge : public BadgeContract<Badge> {
public:
    omui::Size measure(omui::MeasureContext&, std::optional<float>, std::optional<float>, omui::layout::Available,
                       omui::layout::Available) {
        return {props.count > 9 ? 28.0f : 20.0f, 20.0f};
    }
    void paint(omui::PaintContext& ctx) const;
    void semantics(omui::SemanticsContext&, omui::spec::SemanticNode& node) const {
        if (node.name.empty()) node.name = props.label;
        node.value = omui::Value(props.count);
    }
    omui::ActResult dismiss(omui::ActContext&) {
        const std::int32_t was = props.count;
        write_count(0);
        emit_cleared(was);
        return omui::ActResult::done(omui::Value(was));
    }
};

}  // namespace acme::ui
