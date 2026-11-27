// mlibs.cts:slots — the slot contract's cases over one slot (mlibs.compose, D-238): what its owner says of it
// (an id `owner.name`, a summary, the way out for a program without a provider), and how it behaves empty and
// filled — absent is refused naming the slot and the way out, the same whether nobody installed a provider,
// the program left it out or the platform has none; a provider installed is the one read; a slot of one takes
// one; a frozen composition takes no more. Each case runs in a composition of its own.
//
//     mlibs::cts::against({.subject = "omui.accessibility"}, [] {
//         mlibs::cts::slots::declare("omui.accessibility@1", omui::accessibility::service_slot);              // empty
//         mlibs::cts::slots::declare("omui.accessibility@1", omui::accessibility::service_slot, a_service);  // and filled
//     });
export module mlibs.cts:slots;

import std;
import mlibs.compose;
import mlibs.error;
import :core;

export namespace mlibs::cts::slots {

// What is wrong with what an owner says of its slot, a line each; none: it holds.
[[nodiscard]] inline std::vector<std::string> problems(const mlibs::compose::SlotInfo& slot) {
    std::vector<std::string> out;
    const std::string_view id = slot.id;
    const std::size_t dot = id.rfind('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 == id.size())
        out.push_back(std::format("the id `{}` is not `owner.name`", id));
    for (const char letter : id)
        if (!((letter >= 'a' && letter <= 'z') || (letter >= '0' && letter <= '9') || letter == '.' || letter == '_' || letter == '-')) {
            out.push_back(std::format("the id `{}` has `{}`: lower-case letters, digits, `.`, `_`, `-`", id, letter));
            break;
        }
    if (slot.version < 1) out.push_back(std::format("`{}`: version {} is not 1 or more", id, slot.version));
    if (slot.summary.empty()) out.push_back(std::format("`{}`: says nothing of what is provided at it", id));
    if (slot.absent.empty()) out.push_back(std::format("`{}`: no way out for a program without a provider", id));
    return out;
}

// The cases `slot.declared` and `slot.absent`: the slot as its owner says it, and empty.
template <class Service>
void declare(std::string_view contract, const mlibs::compose::Slot<Service>& slot, std::source_location at = std::source_location::current()) {
    const mlibs::compose::SlotInfo info = slot.info;
    mlibs::cts::declare({.id = "slot.declared",
                         .contract = std::string(contract),
                         .level = Level::semantic,
                         .needs = {},
                         .body =
                             [info](Check& check) {
                                 for (const std::string& problem : problems(info)) check.that(false, problem);
                             }},
                        at);
    mlibs::cts::declare(
        {.id = "slot.absent",
         .contract = std::string(contract),
         .level = Level::semantic,
         .needs = {},
         .body =
             [slot](Check& check) {
                 using mlibs::compose::Absence;
                 mlibs::compose::Composition composition;
                 check.that(composition.get(slot) == nullptr && composition.all(slot).empty(), "an empty slot reads as nothing");
                 const mlibs::compose::Error unsaid = composition.missing(slot.info);
                 check.that(unsaid.code == mlibs::compose::Errc::absent && unsaid.kind() == mlibs::error::Kind::refused, "absent is refused");
                 check.that(unsaid.message.contains(slot.info.id), "the refusal names the slot");
                 check.that(slot.info.absent.empty() || unsaid.message.contains(slot.info.absent), "the refusal says the way out");
                 composition.leave_out(slot.info, Absence::left_out);
                 check.that(composition.get(slot) == nullptr && composition.missing(slot.info).code == mlibs::compose::Errc::absent,
                            "left out of the program: the same refusal");
                 composition.leave_out(slot.info, Absence::platform);
                 check.that(composition.get(slot) == nullptr && composition.missing(slot.info).code == mlibs::compose::Errc::absent,
                            "a platform without one: the same refusal");
                 const std::vector<mlibs::compose::Described> said = composition.describe();
                 check.that(said.size() == 1 && said.front().slot == slot.info.id && said.front().providers.empty() &&
                                said.front().way == slot.info.absent,
                            "described as absent, with the way out");
             }},
        at);
}

// And the case `slot.installed`: `service` installed is the one read; a slot of one takes one, a slot of
// many keeps the order; a frozen composition takes no more. `service` outlives the case.
template <class Service>
void declare(std::string_view contract, const mlibs::compose::Slot<Service>& slot, const Service& service,
             std::source_location at = std::source_location::current()) {
    declare(contract, slot, at);
    mlibs::cts::declare(
        {.id = "slot.installed",
         .contract = std::string(contract),
         .level = Level::semantic,
         .needs = {},
         .body =
             [slot, installed = &service](Check& check) {
                 mlibs::compose::Composition composition;
                 check.that(composition.install(slot, *installed, "first").has_value(), "a provider is installed");
                 check.that(composition.get(slot) == installed && composition.provider(slot.info) == "first", "and is the one read");
                 const auto second = composition.install(slot, *installed, "second");
                 if (slot.info.cardinality == mlibs::compose::Cardinality::one)
                     check.that(!second.has_value() && second.error().code == mlibs::compose::Errc::occupied && composition.all(slot).size() == 1,
                                "a slot of one takes one");
                 else check.that(second.has_value() && composition.all(slot).size() == 2, "a slot of many takes each, in order");
                 composition.freeze();
                 const auto late = composition.install(slot, *installed, "late");
                 check.that(!late.has_value() && late.error().code == mlibs::compose::Errc::frozen, "frozen: no more");
                 check.that(composition.get(slot) == installed, "and what is there is read");
             }},
        at);
}

}  // namespace mlibs::cts::slots
