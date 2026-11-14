import std;
import omui;
import omui.site;
import mcpplibs.mtest;

using namespace omui;
using namespace mcpplibs::mtest;

namespace {
constexpr std::string_view panel = "main/body/guide/";
struct BoundSite {
    std::shared_ptr<AgentAccess> access = std::make_shared<AgentAccess>();
    Headless host{HeadlessOptions{.size = {1280, 800}}};
    explicit BoundSite(bool initially_granted = false) {
        access->set_policy({.mode = AgentMode::dual_user, .source = "bound-site-guide-fixture"});
        if (initially_granted) access->give(openas::parse_grant("agent:guide=external@open_link").value());
        host.runtime().set_agent_access(access);
        auto app = site::app("docs", "en");
        host.runtime().set_theme(app.theme);
        app.setup(host.runtime());
        for (auto& verb : app.verbs) expect(host.runtime().verbs().add(verb.desc, verb.fn).has_value());
        host.mount("main", app.view());
        host.settle(seconds(0.6));
        expect(host.click("main/bar/guide_toggle"));
        host.settle(seconds(0.5));
        expect(host.runtime().agent_access_binding() == access);
    }
    bool visible(std::string_view tail) {
        const auto node = host.tree().node(std::string(panel) + std::string(tail));
        return node && !node->states.has(State::hidden);
    }
    std::string text(std::string_view tail) {
        const auto node = host.tree().node(std::string(panel) + std::string(tail));
        return node ? node->name : std::string();
    }
    bool press(std::string_view tail) {
        const auto id = std::string(panel) + std::string(tail);
        expect(host.tree().act(id, "scroll_into_view").exit_code() == 0);
        host.settle(seconds(0.5));
        const bool pressed = host.click(id);
        host.advance(seconds(1));
        host.settle(seconds(0.6));
        return pressed;
    }
    void link() {
        expect(press("tasks/task_link")) << "a real click starts the scripted external-link request";
        host.advance(seconds(4));
        host.settle(seconds(0.8));
    }
};
}

static UTest cases{"site.guide_access", [] {
    "Allow for this visit authorizes the bound site and leaves another instance asking"_test = [] {
        BoundSite a, b;
        a.link();
        expect(a.visible("asking") && a.host.runtime().opened().empty());
        expect(a.text("said/transcript").contains("\"exitCode\":2")) << "the actual scripted Protocol request needs an answer";
        expect(a.press("asking/answer/allow")) << "the person's real Allow for this visit control";
        expect(a.host.runtime().opened() == std::vector<std::string>{"https://github.com/openlifa/openas"}) << "the waiting external request now executes";
        expect(!a.visible("asking")) << "the same bound policy accepted the grant";
        expect(a.text("held/grants") == "Grants: agent:guide=external@open_link");
        b.link();
        expect(b.visible("asking") && b.host.runtime().opened().empty()) << "another dual-user instance still asks";
        expect(b.press("asking/answer/deny"));
        // Keep the baseline's first authorization error from cascading into
        // clicks on a task correctly disabled while it is still asking.
        if (a.visible("asking") || a.host.runtime().opened().size() != 1) return;
        a.link();
        expect(a.host.runtime().opened().size() == 2 && !a.visible("asking")) << "the next actual scripted request also runs within the visit grant";
        expect(a.press("held/revoke")) << "the person's real revoke control";
        expect(a.text("held/grants") == "Grants: none");
        a.link();
        expect(a.visible("asking") && a.host.runtime().opened().size() == 2) << "revoking this instance makes its next request ask again";
    };
    "revoking through one site's UI does not revoke another bound site's grant"_test = [] {
        // The provider's initial grants are a legitimate fixture. The
        // assertions exercise actual Script requests and the real revoke UI.
        BoundSite a(true), b(true);
        a.link(); b.link();
        expect(a.host.runtime().opened().size() == 1 && !a.visible("asking"));
        expect(b.host.runtime().opened().size() == 1 && !b.visible("asking"));
        expect(a.press("held/revoke"));
        a.link();
        expect(a.visible("asking") && a.host.runtime().opened().size() == 1) << "the local site's actual next request is no longer granted";
        b.link();
        expect(!b.visible("asking") && b.host.runtime().opened().size() == 2) << "the other site's actual request remains granted";
        expect(a.text("said/transcript").contains("\"exitCode\":2"));
    };
}};
