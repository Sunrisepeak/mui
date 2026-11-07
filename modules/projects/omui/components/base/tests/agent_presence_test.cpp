// Another actor at work, seen (the site plan §9.5, C2): the model from the
// journal — who, the node it acts on now and its box, the steps before, its
// request waiting for the person — and the reticle drawn from it over the
// page, taking no pointer, saying in the tree what it shows, answering
// nothing for the person.
import std;
import omui.ui.base;
import omui.host.headless;
import mlibs.hash;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;

namespace {

mlibs::text::FontCollection& fonts() {
    static mlibs::text::FontCollection f = [] {
        mlibs::text::FontCollection c;
        (void)c.add_defaults();
        return c;
    }();
    return f;
}

std::int64_t act(Headless& h, std::string_view id, std::string_view action, Value args = {}, std::string answer = {}) {
    Invocation call;
    call.args = std::move(args);
    call.actor = {"agent", "guide", {}};
    call.answer = std::move(answer);
    auto e = h.tree().act(id, action, call);
    h.settle();
    return e.exit_code().value_or(-1);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"ui.base.agent_presence"};
    register_base_components();

    s.test("the model and the reticle: where the agent acts now, the steps before, what it waits for; over the page, taking no pointer", [] {
        Headless h(HeadlessOptions{.size = {480, 260}, .fonts = &fonts()});
        int saved = 0, deleted = 0;
        (void)h.runtime().verbs().add({"doc.delete", "Delete", Effect::irreversible, "delete", {}}, [&](const Value&) -> VerbResult {
            ++deleted;
            return Value{};
        });
        auto volume = signal(40.0);
        h.mount("main", stack({.id = "page", .width = 480.0f, .height = 260.0f},
                              {column({.id = "body", .padding = 24, .gap = 16},
                                      {row({.id = "actions", .gap = 12},
                                           {button({.id = "save", .text = "Save", .variant = ButtonVariant::primary, .on_activated = [&] { ++saved; }}),
                                            button({.id = "delete", .text = "Delete", .variant = ButtonVariant::danger, .verb = "doc.delete"})}),
                                       slider({.id = "volume", .label = "Volume", .value = volume, .width = 200})}),
                               agent_presence({.id = "presence", .asking = "delete the document"})}));
        h.settle();
        Runtime& rt = h.runtime();
        const NodeId presence = rt.find("main/presence");
        check(rt.valid(presence), "it is there");
        check(h.tree().node("main/presence")->role == Role::status && h.tree().node("main/presence")->name.empty(), "no one has acted: it says nothing");
        const auto no_one = agent_presence_state(rt, "agent:");
        check(!no_one.now && no_one.actor.empty());

        // The agent saves: the frame round Save, its name and the action.
        check(act(h, "main/body/actions/save", "activate") == 0 && saved == 1);
        auto now = agent_presence_state(rt, "agent:");
        check(now.actor == "agent:guide" && now.now && now.now->path == "main/body/actions/save" && !now.asking);
        check(now.now->bounds == rt.screen_bounds(rt.find("main/body/actions/save")), "its box, in window coordinates");
        check_eq(h.tree().node("main/presence")->name, std::string("guide activate …/actions/save"));
        // Over the page and taking no pointer: a click there reaches the button.
        const Rect save = rt.screen_bounds(rt.find("main/body/actions/save"));
        check(rt.hit_test(save.center()) == rt.find("main/body/actions/save"), "the pointer goes through it");

        // Then the volume, dragged in several writes: one step a node.
        for (const double v : {50.0, 60.0, 70.0}) check(act(h, "main/body/volume", "set_value", Value{}.set("value", v)) == 0);
        now = agent_presence_state(rt, "agent:");
        check(now.now && now.now->path == "main/body/volume" && now.trail.size() == 1 && now.trail[0].path == "main/body/actions/save",
              std::format("the slider now, Save before ({} steps)", now.trail.size()));
        check_eq(h.tree().node("main/presence")->value.get("trail")->list()->size(), std::size_t{1});

        // It asks: deleting is irreversible. The card says so; nothing on it answers.
        check(act(h, "main/body/actions/delete", "activate") == spec::exit_code::needs_answer && deleted == 0);
        now = agent_presence_state(rt, "agent:");
        check(now.asking && now.now->path == "main/body/actions/delete" && now.trail.size() == 2);
        check_eq(h.tree().node("main/presence")->name, std::string("guide asks you: delete the document"));
        check(h.tree().node("main/presence")->actions.empty(), "it offers no action: the answer is the person's (S-10)");
        // What it draws: a frame round Delete and the card, within the page.
        bool framed = false;
        for (const auto& command : rt.display_list().commands())
            if (std::holds_alternative<xrender::StrokePath>(command)) framed = true;
        check(framed, "the frame's corners drawn");
        (void)h.save_png(scratch_dir("agent-presence") / "asking.png");
        // Answered by the person's own way (the application's control): it acts on.
        check(act(h, "main/body/actions/delete", "activate", {}, "delete") == 0 && deleted == 1);
        check(!agent_presence_state(rt, "agent:").asking, "no longer waiting");

        // Another skin draws the same model: with none, nothing drawn, the same said.
        rt.set(presence, "skin", Value("none"));
        h.settle();
        check_eq(h.tree().node("main/presence")->name, std::string("guide activate …/actions/delete"));
        // The person's own actions are not the agent's.
        check(h.click("main/body/actions/save") && saved == 2);
        h.settle();
        check(agent_presence_state(rt, "agent:").now->path == "main/body/actions/delete", "the person's click is not shown as the agent's");
    });

    return s.run(argc, argv);
}
