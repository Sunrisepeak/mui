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
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
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

std::uint64_t hash_of(const xrender::raster::Surface& pixels) {
    Hash hash;
    for (int y = 0; y < pixels.height(); ++y) hash.bytes(std::as_bytes(std::span(pixels.row(y), static_cast<std::size_t>(pixels.width()))));
    return hash.value();
}

}  // namespace

UTest cases{"ui.base.agent_presence", [] {
    register_base_components();

    "the model and the reticle: where the agent acts now, the steps before, what it waits for; over the page, taking no pointer"_test = [] {
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
        expect(rt.valid(presence)) << "it is there";
        expect(h.tree().node("main/presence")->role == Role::status && h.tree().node("main/presence")->name.empty()) << "no one has acted: it says nothing";
        const auto no_one = agent_presence_state(rt, "agent:");
        expect(!no_one.now && no_one.actor.empty());

        // The agent saves: the frame round Save, its name and the action.
        expect(act(h, "main/body/actions/save", "activate") == 0 && saved == 1);
        auto now = agent_presence_state(rt, "agent:");
        expect(now.actor == "agent:guide" && now.now && now.now->path == "main/body/actions/save" && !now.asking);
        expect(now.now->bounds == rt.screen_bounds(rt.find("main/body/actions/save"))) << "its box, in window coordinates";
        expect(eq(h.tree().node("main/presence")->name, std::string("guide activate …/actions/save")));
        // Over the page and taking no pointer: a click there reaches the button.
        const Rect save = rt.screen_bounds(rt.find("main/body/actions/save"));
        expect(rt.hit_test(save.center()) == rt.find("main/body/actions/save")) << "the pointer goes through it";

        // Then the volume, dragged in several writes: one step a node.
        for (const double v : {50.0, 60.0, 70.0}) expect(act(h, "main/body/volume", "set_value", Value{}.set("value", v)) == 0);
        now = agent_presence_state(rt, "agent:");
        expect(now.now && now.now->path == "main/body/volume" && now.trail.size() == 1 && now.trail[0].path == "main/body/actions/save") << std::format("the slider now, Save before ({} steps)", now.trail.size());
        expect(eq(h.tree().node("main/presence")->value.get("trail")->list()->size(), std::size_t{1}));

        // It asks: deleting is irreversible. The card says so; nothing on it answers.
        expect(act(h, "main/body/actions/delete", "activate") == spec::exit_code::needs_answer && deleted == 0);
        now = agent_presence_state(rt, "agent:");
        expect(now.asking && now.now->path == "main/body/actions/delete" && now.trail.size() == 2);
        expect(eq(h.tree().node("main/presence")->name, std::string("guide asks you: delete the document")));
        expect(h.tree().node("main/presence")->actions.empty()) << "it offers no action: the answer is the person's (S-10)";
        // What it draws: a frame round Delete and the card, within the page.
        bool framed = false;
        for (const auto& command : rt.display_list().commands())
            if (std::holds_alternative<xrender::StrokePath>(command)) framed = true;
        expect(framed) << "the frame's corners drawn";
        (void)h.save_png(scratch_dir("agent-presence") / "asking.png");
        // Answered by the person's own way (the application's control): it acts on.
        expect(act(h, "main/body/actions/delete", "activate", {}, "delete") == 0 && deleted == 1);
        expect(!agent_presence_state(rt, "agent:").asking) << "no longer waiting";

        // Another skin draws the same model: with none, nothing drawn, the same said.
        rt.set(presence, "skin", Value("none"));
        h.settle();
        expect(eq(h.tree().node("main/presence")->name, std::string("guide activate …/actions/delete")));
        // The person's own actions are not the agent's.
        expect(h.click("main/body/actions/save") && saved == 2);
        h.settle();
        expect(agent_presence_state(rt, "agent:").now->path == "main/body/actions/delete") << "the person's click is not shown as the agent's";
        // The person takes over (S-14): it says so; handed back, it no longer does.
        expect(rt.invoke("agents.take_over").exit_code() == 0);
        h.settle();
        expect(agent_presence_state(rt, "agent:").taken_over) << "taken over";
        expect(eq(h.tree().node("main/presence")->name, std::string("You have taken over from guide")));
        expect(act(h, "main/body/actions/save", "activate") != 0 && saved == 2) << "the agent waits";
        expect(rt.invoke("agents.hand_back").exit_code() == 0);
        h.settle();
        expect(!agent_presence_state(rt, "agent:").taken_over && act(h, "main/body/actions/save", "activate") == 0 && saved == 3) << "handed back";
        // What it says it is about to do, then hands back with (openas §4.4, §5.3): shown, as its own; another agent's not.
        rt.set(presence, "skin", Value("reticle"));
        rt.note({.actor = "agent:other", .text = "Not mine to show"});
        rt.note({.actor = "agent:guide", .text = "Save, then close the report", .node = "main/body/actions/save"});
        h.settle();
        expect(eq(h.tree().node("main/presence")->name, std::string("guide: Save, then close the report")));
        rt.note({.actor = "agent:guide", .text = "Saved twice; the report is still open: close it?", .hands_back = true});
        h.settle();
        expect(eq(h.tree().node("main/presence")->name, std::string("guide hands back: Saved twice; the report is still open: close it?")));
        (void)h.save_png(scratch_dir("agent-presence") / "hands-back.png");
        rt.note({.actor = "agent:guide"});
        h.settle();
        expect(eq(h.tree().node("main/presence")->name, std::string("guide activate …/actions/save")));
    };

    "several agents at once (D-160): each its own colour in the order they came, its steps and what it holds — a dashed outline with its name and what for — said in the tree"_test = [] {
        Headless h(HeadlessOptions{.size = {520, 300}, .fonts = &fonts()});
        h.mount("main", stack({.id = "page", .width = 520.0f, .height = 300.0f},
                              {column({.id = "body", .padding = 24, .gap = 18},
                                      {row({.id = "actions", .gap = 12},
                                           {button({.id = "save", .text = "Save", .variant = ButtonVariant::primary}), button({.id = "share", .text = "Share"})}),
                                       column({.id = "appearance", .width = 260, .gap = 8},
                                              {toggle({.id = "dark", .label = "Dark"}), slider({.id = "contrast", .label = "Contrast", .value = 50.0, .width = 220})})}),
                               agent_presence({.id = "presence"})}));
        h.settle();
        Runtime& rt = h.runtime();
        const auto as = [&](std::string_view name, std::string_view node, std::string_view action) {
            Invocation call;
            call.actor = {"agent", std::string(name), {}};
            (void)h.tree().act(node, action, call);
            h.settle();
        };
        as("guide", "main/body/actions/save", "focus");
        as("scout", "main/body/appearance/dark", "toggle");
        expect(rt.claim({.node = "main/body/appearance", .subtree = true, .intent = "a dark look"}, {"agent", "scout", {}}).has_value());
        h.settle();
        const auto node = h.tree().node("main/presence");
        expect(fatal(node.has_value()));
        const Value* agents = node->value.get("agents");
        expect(fatal(agents && agents->list() && agents->list()->size() == 2)) << node->value.json();
        const Value& first = agents->list()->at(0);
        const Value& second = agents->list()->at(1);
        expect(first.get("actor")->as_string() == "agent:guide" && first.get("hue")->as_int() == 0 && first.get("node")->as_string() == "main/body/actions/save");
        expect(second.get("actor")->as_string() == "agent:scout" && second.get("hue")->as_int() == 1 && second.get("claims")->list()->size() == 1 &&
               second.get("claims")->list()->at(0).get("intent")->as_string() == "a dark look")
            << second.json();
        expect(agent_ink(rt.theme(), 0) != agent_ink(rt.theme(), 1)) << "a colour each";
        // Drawn: the claim's outline in scout's colour, the two frames in theirs.
        const std::uint64_t both = hash_of(h.pixels());
        (void)h.save_png(scratch_dir("agent-presence") / "several.png");
        expect(rt.release_claim("main/body/appearance", "", {"agent", "scout", {}}));
        h.settle();
        const std::uint64_t released = hash_of(h.pixels());
        expect(both != released) << "the outline goes with the claim";
        expect(h.tree().node("main/presence")->value.get("agents")->list()->at(1).get("claims")->list()->empty());
    };
}};
