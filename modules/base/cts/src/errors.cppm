// mlibs.cts:errors — the error contract's case over one domain's codes (convergence plan §6 item 3, D-186,
// RC2.19): every code of the domain is described — a name `domain.code` (one domain, no name twice), a
// sentence of what went wrong, and its way out: what the caller does next. An agent that meets the error acts on the way out;
// one that is empty leaves it guessing.
//
//     mlibs::cts::against({.subject = "xengine.matter"}, [] {
//         mlibs::cts::errors::declare("xengine.matter@1", xengine::matter::Errc::unstable);   // the last code
//     });
//
// A domain's codes are its enum's values from 0 to the last, as the enums are declared (no values given).
export module mlibs.cts:errors;

import std;
import mlibs.error;
import :core;

export namespace mlibs::cts::errors {

// What is wrong with one domain's descriptions, a line each; none: they hold.
template <mlibs::error::Code C>
[[nodiscard]] std::vector<std::string> problems(C last) {
    std::vector<std::string> out;
    std::set<std::string_view> names;
    std::string_view domain;
    using Underlying = std::underlying_type_t<C>;
    for (Underlying value = 0; value <= static_cast<Underlying>(last); ++value) {
        const mlibs::error::Description description = describe(static_cast<C>(value));
        const std::string_view name = description.name;
        const std::size_t dot = name.find('.');
        if (dot == std::string_view::npos || dot == 0 || dot + 1 == name.size()) {
            out.push_back(std::format("code {}: the name `{}` is not `domain.code`", value, name));
            continue;
        }
        if (domain.empty()) domain = name.substr(0, dot);
        if (name.substr(0, dot) != domain) out.push_back(std::format("`{}`: another domain than `{}`", name, domain));
        if (!names.insert(name).second) out.push_back(std::format("`{}`: named twice", name));
        if (description.text.empty()) out.push_back(std::format("`{}`: says nothing of what went wrong", name));
        if (description.way.empty()) out.push_back(std::format("`{}`: no way out", name));
    }
    return out;
}

// The case `error.codes`: every code of the domain described, each with its way out.
template <mlibs::error::Code C>
void declare(std::string_view contract, C last, std::source_location at = std::source_location::current()) {
    mlibs::cts::declare({.id = "error.codes",
                         .contract = std::string(contract),
                         .level = Level::semantic,
                         .needs = {},
                         .body =
                             [last](Check& check) {
                                 for (const std::string& problem : problems(last)) check.that(false, problem);
                             }},
                        at);
}

}  // namespace mlibs::cts::errors
