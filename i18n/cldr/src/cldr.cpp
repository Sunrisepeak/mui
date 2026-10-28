module openmui.i18n.cldr;

import std;
import openmui.i18n;

namespace omui::i18n::cldr {

namespace {

struct Packs {
    std::vector<std::pair<std::string_view, std::shared_ptr<const LocaleData>>> data;
    LikelySubtags                                                                likely;
};

// Read once; the packs are this package's own data, so a problem in one is a
// build's mistake: it is reported, and the pack left out.
const Packs& packs() {
    static const Packs p = [] {
        Packs out;
        for (auto [id, text] : {std::pair{std::string_view("en"), en_locale}, std::pair{std::string_view("zh"), zh_locale},
                                std::pair{std::string_view("zh-Hant"), zh_Hant_locale}}) {
            auto d = read_locale_data(text, std::format("{}.locale.omui.json", id));
            if (!d) {
                for (const auto& problem : d.error()) std::println(std::cerr, "openmui.i18n.cldr: {}", problem.text());
                continue;
            }
            out.data.emplace_back(id, std::make_shared<const LocaleData>(std::move(*d)));
        }
        auto l = read_likely_subtags(likely_locale);
        if (l) out.likely = std::move(*l);
        else
            for (const auto& problem : l.error()) std::println(std::cerr, "openmui.i18n.cldr: {}", problem.text());
        return out;
    }();
    return p;
}

}  // namespace

std::shared_ptr<const LocaleData> data(std::string_view id) {
    for (const auto& [k, d] : packs().data)
        if (k == id) return d;
    return nullptr;
}

const LikelySubtags& likely() { return packs().likely; }

void add_to(Library& library) {
    for (const auto& [k, d] : packs().data) library.add(d);
    library.set_likely(likely());
}

}  // namespace omui::i18n::cldr
