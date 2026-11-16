// A package's contract became its C++ at build time: the typed options, the
// two-way count, the enum, the event and the action all exist and work.
import std;
import acme.ui;
import omui.host.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

UTest cases{"plugins.contracts", [] {

    "the generated badge is a component like any other"_test = [] {
        omui::Headless h({.size = {200, 100}});
        auto count = omui::signal(std::int32_t{12});
        std::vector<std::int32_t> cleared;
        h.mount("main", omui::column({.padding = 10}, {
                                         acme::ui::badge({.id = "inbox", .count = count, .label = "Unread", .tone = acme::ui::BadgeTone::bad,
                                                          .on_cleared = [&](std::int32_t was) { cleared.push_back(was); },
                                                          .align_self = "start"}),
                                     }));
        h.settle();
        auto n = h.tree().node("main/inbox");
        expect(n && n->role == omui::Role::status && n->name == "Unread" && n->value == omui::Value(12)) << (n ? omui::spec::to_value(*n).json() : "none");
        expect(acme::ui::Badge::contract().name == "badge" && acme::ui::Badge::contract().action("dismiss"));
        expect(h.runtime().bounds(h.runtime().find("main/inbox")).width == 28);
        omui::Invocation call;
        expect(h.tree().act("main/inbox", "dismiss", call).exit_code() == std::optional<int>(0));
        expect(count.peek() == 0 && cleared == std::vector<std::int32_t>{12}) << "two-way, and the event";
        expect(acme::ui::name(acme::ui::BadgeTone::good) == "good");
        expect(!acme::ui::parse_badge_tone("loud"));
    };
}};
