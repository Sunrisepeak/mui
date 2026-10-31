// The shots by name, and what a shot reads of its clip.
module openmui.showcase.promo_video;

import std;
import openmui;
import openmui.timeline;
import openmui.showcase.markdown_wall;

namespace omui::promo {

void prepare() {
    static std::once_flag once;
    std::call_once(once, [] {
        add_styles();
        register_base_components();
        wall::register_all();
    });
}

double ShotContext::value(std::string_view property, double t, double otherwise) const {
    if (!clip) return otherwise;
    return clip->value(property, clip->at + tl::from_seconds(t) - clip->in, otherwise);
}

std::vector<std::pair<double, double>> ShotContext::steps(std::string_view property) const {
    std::vector<std::pair<double, double>> out;
    if (!clip) return out;
    for (const tl::Property& p : clip->properties)
        if (p.name == property)
            for (const auto& k : p.keys.keys()) out.emplace_back(k.time.count() + tl::to_seconds(clip->in), k.value);
    return out;
}

double ShotContext::rate() const noexcept {
    return cut ? static_cast<double>(cut->timeline.rate.num) / cut->timeline.rate.den : 60.0;
}

std::expected<std::unique_ptr<Shot>, std::string> make_shot(std::string_view name, const ShotContext& c) {
    if (name == "origin") return origin_shot(c);
    if (name == "code") return code_shot(c);
    if (name == "infinite") return infinite_shot(c);
    if (name == "styles") return styles_shot(c);
    if (name == "agent") return agent_shot(c);
    if (name == "time") return time_shot(c);
    if (name == "end") return end_shot(c);
    return std::unexpected(std::format("no shot is named `{}`", name));
}

}  // namespace omui::promo
