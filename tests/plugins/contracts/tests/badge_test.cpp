// A package's contract became its C++ at build time: the typed options, the
// two-way count, the enum, the event and the action all exist and work.
import std;
import acme.ui;
import openmui.host.headless;
import openmui.testing;

using namespace omui::testing;

int main(int argc, char** argv) {
    Suite s{"plugins.contracts"};

    s.test("the generated badge is a component like any other", [] {
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
        check(n && n->role == omui::Role::status && n->name == "Unread" && n->value == omui::Value(12), n ? omui::spec::to_value(*n).json() : "none");
        check(acme::ui::Badge::contract().name == "badge" && acme::ui::Badge::contract().action("dismiss"));
        check(h.runtime().bounds(h.runtime().find("main/inbox")).width == 28);
        omui::Invocation call;
        check(h.tree().act("main/inbox", "dismiss", call).exit_code() == std::optional<int>(0));
        check(count.peek() == 0 && cleared == std::vector<std::int32_t>{12}, "two-way, and the event");
        check(acme::ui::name(acme::ui::BadgeTone::good) == "good");
        check(!acme::ui::parse_badge_tone("loud"));
    });

    return s.run(argc, argv);
}
