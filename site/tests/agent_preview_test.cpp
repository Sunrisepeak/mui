// Agent presentation: real local actions, progressive detail, bounded preview
// and explicit ownership. Same-frame pre-timer input is tested by the app gate.
import std;
import openmui;
import openmui.site;
import openmui.theme;
import openmui.theme.styles;
import openmui.testing;

using namespace omui;
using namespace omui::testing;

namespace {
struct Fixture {
    site::AgentFeature state;
    Signal<std::string> language = signal(std::string("en"));
    bool blocked = false;
    float width;
    Headless h;
    Runtime* rt;
    site::AgentFeatureContext context;
    Fixture(std::string mode = "use", float room = 600, std::string lang = "en")
        : width(room), h(HeadlessOptions{.size = {room + 32, 2400}, .tier = pipeline::Tier::t4}), rt(&h.runtime()) {
        state.mode.set(std::move(mode)); language.set(std::move(lang));
        context.runtime = &rt; context.lang = &language; context.root = "main/agent_feature";
        context.preview_blocked = [this] { return blocked; };
        context.style = [this](std::string name) { check(theme::apply_style(*rt, name, context.scope, false).has_value()); };
        h.mount("main", column({.width = Dim(room + 32), .padding = Edges(16), .background = "background"}, {site::agent_feature(state, context, room)}));
        h.step();
    }
    std::string path(std::string_view leaf) const { return context.root + "/" + std::string(leaf); }
    bool has(std::string_view leaf) const { return rt->valid(rt->find(path(leaf))); }
    void tick() { site::tick_agent_feature(state, context); h.step(); }
    void advance(double seconds_) { h.advance(seconds(seconds_)); tick(); }
    void act(std::string_view leaf, std::string_view action = "activate", Value args = {}) {
        Invocation call; call.args = std::move(args);
        check(h.tree().act(path(leaf), action, call).exit_code() == std::optional<std::int64_t>(0), std::string(leaf)); h.step();
    }
    Value task() const { return Value{}.set("title", state.title.peek()).set("reviewed", state.reviewed.peek()).set("recorded", state.recorded.peek()); }
    void inside() {
        for (const auto& node : h.tree().snapshot({.root = context.root}).nodes) {
            if (!node.bounds || node.states.has(State::hidden) || node.states.has(State::offscreen)) continue;
            if (node.role != Role::button && node.role != Role::text_box && node.role != Role::checkbox && node.role != Role::slider && node.role != Role::combo_box && node.role != Role::radio) continue;
            check(node.bounds->x >= 15.5f && node.bounds->right() <= width + 16.5f, std::format("{} x={} width={}", node.id, node.bounds->x, node.bounds->width));
        }
    }
};
}

int main(int argc, char** argv) {
    Suite suite{"site.agent-preview"}; (void)themes::add_styles();
    const auto media = scratch_dir("agent-preview");

    suite.test("each mode starts with the real task and mounts parameters only after View details", [media] {
        for (const auto mode : {"use", "develop", "design"}) for (float width : {260.0f, 900.0f}) for (const auto lang : {"en", "zh"}) {
            Fixture f(mode, width, lang); const auto before = f.task();
            check(f.has("specimen/title") && f.has("specimen/reviewed") && f.has("specimen/record") && f.has("specimen/receipt"));
            check(!f.has("develop/expected") && !f.has("develop/requests/read") && !f.has("design/style") && !f.has("design/spacing") && !f.has("method/material") && !f.has("method/expression"));
            const auto toggle=f.h.tree().node(f.path("method/toggle"));
            check(toggle && toggle->name == (std::string_view(lang)=="zh" ? "查看详情" : "View details"));
            check(f.h.tree().node(f.path("heading"))->role == Role::heading);
            f.inside();
            check(f.h.save_png(media / std::format("{}-{}-{}-preview.png", mode, lang, static_cast<int>(width))).has_value());
            f.act("method/toggle");
            check(f.state.preview_interactive.peek() && !f.state.preview_timer);
            if (std::string_view(mode)=="develop") check(f.has("develop/expected") && f.has("develop/requests/read") && f.has("develop/requests/change") && f.has("develop/requests/assert") && f.has("method/expression"));
            if (std::string_view(mode)=="design") check(f.has("design/style") && f.has("design/layout") && f.has("design/spacing") && f.has("method/material"));
            check(f.task()==before, "disclosing detail preserves the current task"); f.inside();
            check(f.h.save_png(media / std::format("{}-{}-{}-detail.png", mode, lang, static_cast<int>(width))).has_value());
            f.act("method/toggle");
            check(!f.has("develop/expected") && !f.has("design/style") && !f.has("method/material"));
        }
    });

    suite.test("each first visit performs one finite real demonstration and then stops", [] {
        for (const auto mode : {"use", "develop", "design"}) {
            Fixture f(mode); const auto title=f.state.title.peek(); const auto stage=f.rt->find(f.path("specimen"));
            f.tick(); check(f.state.running.peek() && f.state.preview_timer != 0);
            f.advance(2.5);
            check(!f.state.running.peek() && f.state.preview_timer==0 && f.state.preview_status.peek()=="complete", mode);
            if (std::string_view(mode)=="use") {
                check(f.state.reviewed.peek() && f.state.recorded.peek() && f.state.requests.peek()>=2);
                check(std::ranges::any_of(f.rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";}));
                check(f.has("history/undo") && f.has("history/new"), "a completed record has explicit editing recovery");
                f.act("history/undo"); check(!f.state.recorded.peek(), "undo makes the recorded task editable again");
            } else if (std::string_view(mode)=="develop") {
                check_eq(f.state.title.peek(),f.state.request_text.peek());
                check(f.state.status.peek().starts_with("PASS") && f.state.requests.peek()>=3);
                check(f.state.request_json.peek().contains("snapshot") && f.state.result_json.peek().contains("result"), "the success came from a real readback");
            } else {
                check_eq(f.state.title.peek(),title); check(!f.state.reviewed.peek() && !f.state.recorded.peek());
                check(f.state.layout.peek()=="cards" && f.state.spacing.peek()==20 && f.state.requests.peek()>=3);
                check(f.rt->valid(stage) && f.has("specimen/title") && f.has("specimen/record"), "layout preserves actual action identity");
            }
            const auto count=f.state.requests.peek(); f.advance(4); check(f.state.requests.peek()==count,"the completed preview does not loop");
        }
    });

    suite.test("pointer focus typing and first touch claim the task without losing input", [] {
        Fixture typing("develop"); typing.tick(); typing.h.advance(seconds(0.1));
        check(typing.h.click(typing.path("specimen/title"))); typing.h.type(" my own idea"); typing.h.step(); typing.tick();
        const auto text=typing.state.title.peek(); check(text.contains("my own idea"));
        check(typing.state.preview_interactive.peek() && !typing.state.running.peek() && typing.state.preview_timer==0);
        typing.h.move({0,0}); typing.rt->focus({}); typing.advance(3);
        check_eq(typing.state.title.peek(),text); check_eq(typing.state.requests.peek(),std::int64_t{0});
        Fixture keyboard; keyboard.tick(); keyboard.rt->focus_visibly(keyboard.rt->find(keyboard.path("specimen/reviewed"))); keyboard.h.step(); keyboard.tick();
        check(keyboard.state.preview_interactive.peek() && keyboard.state.preview_timer==0);
        keyboard.advance(3); check(!keyboard.state.recorded.peek());
        Fixture touch; touch.tick(); const auto at=touch.rt->screen_bounds(touch.rt->find(touch.path("specimen/reviewed"))).center();
        PointerEvent down; down.kind=PointerKind::down; down.position=at; down.type=PointerType::touch; down.button=MouseButton::left; down.buttons=1;
        auto up=down; up.kind=PointerKind::up; up.buttons=0;
        touch.h.step({down,up}); touch.tick();
        check(touch.state.preview_interactive.peek() && touch.state.reviewed.peek(), "the first touch reaches the actual checkbox");
        touch.advance(3); check(!touch.state.recorded.peek() && touch.state.preview_timer==0);
    });

    suite.test("external writes between steps cancel rather than overwrite a newer task", [] {
        Fixture f("develop"); f.tick(); f.advance(0.5); check_eq(f.state.requests.peek(),std::int64_t{1});
        f.state.title.set("newer external value"); f.h.step(); f.advance(2);
        check_eq(f.state.title.peek(),std::string("newer external value"));
        check(f.state.preview_interactive.peek() && !f.state.running.peek() && f.state.preview_timer==0);
        check_eq(f.state.requests.peek(),std::int64_t{1});
    });

    suite.test("pause occlusion offscreen route removal and motion preferences require explicit replay", [] {
        for (const std::string reason : {"pause","blocked","offscreen","off","reduced","host-reduced","removed"}) {
            Fixture f("develop"); f.tick(); check(f.state.preview_timer!=0);
            if(reason=="pause")f.act("preview/actions/pause");
            if(reason=="blocked")f.blocked=true;
            if(reason=="offscreen")f.rt->set(f.rt->find(f.path("specimen")),"translate_y",Value(-4000.0));
            if(reason=="off")f.rt->set_motion_level(MotionLevel::off);
            if(reason=="reduced")f.rt->set_motion_level(MotionLevel::reduced);
            if(reason=="host-reduced")f.rt->set_reduce_motion(true);
            if(reason=="removed")f.h.mount("main",box({.id="elsewhere",.width=300,.height=200}));
            f.h.step(); f.tick();
            check(!f.state.running.peek() && f.state.preview_timer==0,reason);
            f.advance(2); check(f.state.requests.peek()==0,reason);
            if(reason=="removed")continue;
            f.blocked=false; f.rt->set_motion_level(MotionLevel::full); f.rt->set_reduce_motion(false);
            f.rt->set(f.rt->find(f.path("specimen")),"translate_y",Value(0.0)); f.h.step(); f.tick(); f.advance(1);
            check(f.state.requests.peek()==0,"returning does not silently resume: "+reason);
            f.act("preview/actions/replay"); f.advance(2.5);
            check(f.state.status.peek().starts_with("PASS"),"explicit Replay remains reachable: "+reason);
        }
    });

    suite.test("switching mode cancels old actions and replay keeps an already recorded task", [] {
        Fixture f; f.tick(); f.h.advance(seconds(0.1)); f.act("modes","set_value",Value{}.set("value","develop")); f.tick(); f.advance(2.5);
        check(!f.state.reviewed.peek() && !f.state.recorded.peek());
        check(f.state.status.peek().starts_with("PASS"));
        check(std::ranges::none_of(f.rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";}),"old use steps cannot run in the new mode");
        Fixture complete; complete.tick(); complete.advance(2.5); const auto before=complete.task();
        complete.act("use/actions/play"); complete.advance(1);
        check(complete.task()==before && !complete.state.running.peek(),"replay reads the completed task without clearing its local receipt");
        complete.act("history/new"); check(!complete.state.recorded.peek() && !complete.state.reviewed.peek());
        check_eq(complete.state.title.peek(),before.get("title")->as_string());
    });

    suite.test("off and reduced retain manual real task actions and full protocol detail", [] {
        for (const auto level : {MotionLevel::off,MotionLevel::reduced}) {
            Fixture f; f.rt->set_motion_level(level); f.tick();
            check(!f.state.running.peek() && !f.state.preview_timer);
            f.act("specimen/reviewed","toggle"); f.act("specimen/record");
            check(f.state.recorded.peek() && f.state.preview_interactive.peek());
            f.act("history/new"); f.act("modes","set_value",Value{}.set("value","develop")); f.act("method/toggle");
            f.act("develop/requests/assert"); check(f.state.status.peek().starts_with("FAIL"));
            f.act("develop/requests/change"); f.act("develop/requests/assert");
            check(f.state.status.peek().starts_with("PASS"));
            Fixture scripted; scripted.rt->set_motion_level(level); scripted.tick();
            scripted.act("use/actions/play"); scripted.advance(2);
            check(scripted.state.reviewed.peek() && scripted.state.recorded.peek() && !scripted.state.running.peek(),
                  "an explicitly requested simulation still completes real actions without automatic motion");
        }
    });

    suite.test("explicit tasks retain manual authorization across reduced-motion ticks and yield to new input", [] {
        for (const auto level : {MotionLevel::off, MotionLevel::reduced}) {
            Fixture develop("develop"); develop.rt->set_motion_level(level); develop.tick();
            develop.act("preview/actions/run"); develop.tick();
            check(develop.state.preview_manual && develop.state.running.peek(), "a normal coordinator tick preserves explicit authorization");
            develop.advance(1);
            check(develop.state.status.peek().starts_with("PASS") && develop.state.requests.peek()>=3);
            check_eq(develop.state.title.peek(),develop.state.request_text.peek());
            Fixture design("design"); design.rt->set_motion_level(level); design.tick(); const auto data=design.task();
            design.act("preview/actions/run"); design.tick();
            check(design.state.layout.peek()=="cards" && design.task()==data, "the manual layout action works without animation and preserves task data");
            design.act("preview/actions/replay"); design.tick(); design.advance(1);
            check(design.state.requests.peek()>=3 && !design.state.running.peek(), "explicit design replay also completes its finite semantic sequence");
            Fixture interrupted("develop"); interrupted.rt->set_motion_level(level); interrupted.tick();
            interrupted.act("preview/actions/run"); interrupted.tick();
            interrupted.act("specimen/title","set_value",Value{}.set("value","a newer manual title")); interrupted.advance(1);
            check_eq(interrupted.state.title.peek(),std::string("a newer manual title"));
            check(interrupted.state.preview_interactive.peek() && !interrupted.state.preview_timer && !interrupted.state.running.peek());
        }
    });

    suite.test("destroying a model with a pending preview leaves no dangling timer access", [] {
        Headless h(HeadlessOptions{.size={640,1400}}); Runtime* rt=&h.runtime();
        auto model=std::make_unique<site::AgentFeature>();
        site::AgentFeatureContext c; c.runtime=&rt; c.root="main/agent_feature";
        h.mount("main",column({.width=640,.padding=16},{site::agent_feature(*model,c,600)})); h.step();
        site::tick_agent_feature(*model,c); check(model->preview_timer!=0);
        h.mount("main",box({.id="another",.width=640,.height=200})); h.step(); model.reset();
        h.advance(seconds(3));
        check(std::ranges::none_of(rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";}));
    });
    return suite.run(argc,argv);
}
