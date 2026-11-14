// Agent presentation: real local actions, progressive detail, bounded preview
// and explicit ownership. Same-frame pre-timer input is tested by the app gate.
import std;
import omui;
import omui.site;
import omui.theme;
import omui.theme.styles;
import mlibs.testing;
import mcpplibs.mtest;

using namespace omui;
using namespace mlibs::testing;
using namespace mcpplibs::mtest;

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
        : width(room), h(HeadlessOptions{.size = {room + 32, 2400}, .tier = mlibs::pipeline::Tier::t4}), rt(&h.runtime()) {
        state.mode.set(std::move(mode)); language.set(std::move(lang));
        context.runtime = &rt; context.lang = &language; context.root = "main/agent_feature";
        context.preview_blocked = [this] { return blocked; };
        context.style = [this](std::string name) { expect(theme::apply_style(*rt, name, context.scope, false).has_value()); };
        h.mount("main", column({.width = Dim(room + 32), .padding = Edges(16), .background = "background"}, {site::agent_feature(state, context, room)}));
        h.step();
    }
    std::string path(std::string_view leaf) const { return context.root + "/" + std::string(leaf); }
    bool has(std::string_view leaf) const { return rt->valid(rt->find(path(leaf))); }
    void tick() { site::tick_agent_feature(state, context); h.step(); }
    void advance(double seconds_) { h.advance(seconds(seconds_)); tick(); }
    void act(std::string_view leaf, std::string_view action = "activate", Value args = {}) {
        Invocation call; call.args = std::move(args);
        expect(h.tree().act(path(leaf), action, call).exit_code() == std::optional<std::int64_t>(0)) << std::string(leaf); h.step();
    }
    Value task() const { return Value{}.set("title", state.title.peek()).set("reviewed", state.reviewed.peek()).set("recorded", state.recorded.peek()); }
    void inside() {
        for (const auto& node : h.tree().snapshot({.root = context.root}).nodes) {
            if (!node.bounds || node.states.has(State::hidden) || node.states.has(State::offscreen)) continue;
            if (node.role != Role::button && node.role != Role::text_box && node.role != Role::checkbox && node.role != Role::slider && node.role != Role::combo_box && node.role != Role::radio) continue;
            expect(node.bounds->x >= 15.5f && node.bounds->right() <= width + 16.5f) << std::format("{} x={} width={}", node.id, node.bounds->x, node.bounds->width);
        }
    }
};
}

static UTest cases{"site.agent-preview", [] {
     (void)themes::add_styles();
    const auto media = scratch_dir("agent-preview");

    "each mode starts with the real task and mounts parameters only after View details"_test = [media] {
        for (const auto mode : {"use", "develop", "design"}) for (float width : {260.0f, 900.0f}) for (const auto lang : {"en", "zh"}) {
            Fixture f(mode, width, lang); const auto before = f.task();
            expect(f.has("specimen/title") && f.has("specimen/reviewed") && f.has("specimen/record") && f.has("specimen/receipt"));
            expect(!f.has("develop/expected") && !f.has("develop/requests/read") && !f.has("design/style") && !f.has("design/spacing") && !f.has("method/material") && !f.has("method/expression"));
            if (std::string_view(mode) != "use") {
                expect(f.has(std::string_view(mode) == "develop" ? "preview/actions/verify" : "preview/actions/arrange"));
                expect(!f.has(std::string_view(mode) == "develop" ? "preview/actions/arrange" : "preview/actions/verify"));
            }
            const auto toggle=f.h.tree().node(f.path("method/toggle"));
            expect(toggle && toggle->name == (std::string_view(lang)=="zh" ? "查看详情" : "View details"));
            expect(f.h.tree().node(f.path("heading"))->role == Role::heading);
            f.inside();
            expect(f.h.save_png(media / std::format("{}-{}-{}-preview.png", mode, lang, static_cast<int>(width))).has_value());
            f.act("method/toggle");
            expect(f.state.preview_interactive.peek() && !f.state.preview_timer);
            if (std::string_view(mode)=="develop") expect(f.has("develop/expected") && f.has("develop/requests/read") && f.has("develop/requests/change") && f.has("develop/requests/assert") && f.has("method/expression"));
            if (std::string_view(mode)=="design") expect(f.has("design/style") && f.has("design/layout") && f.has("design/spacing") && f.has("method/material"));
            expect(f.task()==before) << "disclosing detail preserves the current task"; f.inside();
            expect(f.h.save_png(media / std::format("{}-{}-{}-detail.png", mode, lang, static_cast<int>(width))).has_value());
            f.act("method/toggle");
            expect(!f.has("develop/expected") && !f.has("design/style") && !f.has("method/material"));
        }
    };

    "each first visit performs one finite real demonstration and then stops"_test = [] {
        for (const auto mode : {"use", "develop", "design"}) {
            Fixture f(mode); const auto title=f.state.title.peek(); const auto stage=f.rt->find(f.path("specimen"));
            f.tick(); expect(f.state.running.peek() && f.state.preview_timer != 0);
            f.advance(2.5);
            expect(!f.state.running.peek() && f.state.preview_timer==0 && f.state.preview_status.peek()=="complete") << mode;
            if (std::string_view(mode)=="use") {
                expect(f.state.reviewed.peek() && f.state.recorded.peek() && f.state.requests.peek()>=2);
                expect(std::ranges::any_of(f.rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";}));
                expect(f.has("history/undo") && f.has("history/new")) << "a completed record has explicit editing recovery";
                f.act("history/undo"); expect(!f.state.recorded.peek()) << "undo makes the recorded task editable again";
            } else if (std::string_view(mode)=="develop") {
                expect(eq(f.state.title.peek(),f.state.request_text.peek()));
                expect(f.state.status.peek().starts_with("PASS") && f.state.requests.peek()>=3);
                expect(f.state.request_json.peek().contains("snapshot") && f.state.result_json.peek().contains("result")) << "the success came from a real readback";
            } else {
                expect(eq(f.state.title.peek(),title)); expect(!f.state.reviewed.peek() && !f.state.recorded.peek());
                expect(f.state.layout.peek()=="cards" && f.state.spacing.peek()==20 && f.state.requests.peek()>=3);
                expect(std::ranges::any_of(f.rt->action_journal(),[&](const auto& entry){ return entry.path==f.path("preview/actions/arrange") && entry.actor=="agent:agent-feature-design" && entry.exit_code==0; })) << "design replay invokes the real renamed arrange action";
                expect(f.rt->valid(stage) && f.has("specimen/title") && f.has("specimen/record")) << "layout preserves actual action identity";
            }
            const auto count=f.state.requests.peek(); f.advance(4); expect(f.state.requests.peek()==count) << "the completed preview does not loop";
        }
    };

    "pointer focus typing and first touch claim the task without losing input"_test = [] {
        Fixture typing("develop"); typing.tick(); typing.h.advance(seconds(0.1));
        expect(typing.h.click(typing.path("specimen/title"))); typing.h.type(" my own idea"); typing.h.step(); typing.tick();
        const auto text=typing.state.title.peek(); expect(text.contains("my own idea"));
        expect(typing.state.preview_interactive.peek() && !typing.state.running.peek() && typing.state.preview_timer==0);
        typing.h.move({0,0}); typing.rt->focus({}); typing.advance(3);
        expect(eq(typing.state.title.peek(),text)); expect(eq(typing.state.requests.peek(),std::int64_t{0}));
        Fixture keyboard; keyboard.tick(); keyboard.rt->focus_visibly(keyboard.rt->find(keyboard.path("specimen/reviewed"))); keyboard.h.step(); keyboard.tick();
        expect(keyboard.state.preview_interactive.peek() && keyboard.state.preview_timer==0);
        keyboard.advance(3); expect(!keyboard.state.recorded.peek());
        Fixture touch; touch.tick(); const auto at=touch.rt->screen_bounds(touch.rt->find(touch.path("specimen/reviewed"))).center();
        PointerEvent down; down.kind=PointerKind::down; down.position=at; down.type=PointerType::touch; down.button=MouseButton::left; down.buttons=1;
        auto up=down; up.kind=PointerKind::up; up.buttons=0;
        touch.h.step({down,up}); touch.tick();
        expect(touch.state.preview_interactive.peek() && touch.state.reviewed.peek()) << "the first touch reaches the actual checkbox";
        touch.advance(3); expect(!touch.state.recorded.peek() && touch.state.preview_timer==0);
    };

    "external writes between steps cancel rather than overwrite a newer task"_test = [] {
        Fixture f("develop"); f.tick(); f.advance(0.5); expect(eq(f.state.requests.peek(),std::int64_t{1}));
        f.state.title.set("newer external value"); f.h.step(); f.advance(2);
        expect(eq(f.state.title.peek(),std::string("newer external value")));
        expect(f.state.preview_interactive.peek() && !f.state.running.peek() && f.state.preview_timer==0);
        expect(eq(f.state.requests.peek(),std::int64_t{1}));
    };

    "pause occlusion offscreen route removal and motion preferences require explicit replay"_test = [] {
        for (const std::string reason : {"pause","blocked","offscreen","off","reduced","host-reduced","removed"}) {
            Fixture f("develop"); f.tick(); expect(f.state.preview_timer!=0);
            if(reason=="pause")f.act("preview/actions/pause");
            if(reason=="blocked")f.blocked=true;
            if(reason=="offscreen")f.rt->set(f.rt->find(f.path("specimen")),"translate_y",Value(-4000.0));
            if(reason=="off")f.rt->set_motion_level(MotionLevel::off);
            if(reason=="reduced")f.rt->set_motion_level(MotionLevel::reduced);
            if(reason=="host-reduced")f.rt->set_reduce_motion(true);
            if(reason=="removed")f.h.mount("main",box({.id="elsewhere",.width=300,.height=200}));
            f.h.step(); f.tick();
            expect(!f.state.running.peek() && f.state.preview_timer==0) << reason;
            f.advance(2); expect(f.state.requests.peek()==0) << reason;
            if(reason=="removed")continue;
            f.blocked=false; f.rt->set_motion_level(MotionLevel::full); f.rt->set_reduce_motion(false);
            f.rt->set(f.rt->find(f.path("specimen")),"translate_y",Value(0.0)); f.h.step(); f.tick(); f.advance(1);
            expect(f.state.requests.peek()==0) << "returning does not silently resume: "+reason;
            f.act("preview/actions/replay"); f.advance(2.5);
            expect(f.state.status.peek().starts_with("PASS")) << "explicit Replay remains reachable: "+reason;
        }
    };

    "switching mode cancels old actions and replay keeps an already recorded task"_test = [] {
        Fixture f; f.tick(); f.h.advance(seconds(0.1)); f.act("modes","set_value",Value{}.set("value","develop")); f.tick(); f.advance(2.5);
        expect(!f.state.reviewed.peek() && !f.state.recorded.peek());
        expect(f.state.status.peek().starts_with("PASS"));
        expect(std::ranges::none_of(f.rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";})) << "old use steps cannot run in the new mode";
        Fixture complete; complete.tick(); complete.advance(2.5); const auto before=complete.task();
        complete.act("use/actions/play"); complete.advance(1);
        expect(complete.task()==before && !complete.state.running.peek()) << "replay reads the completed task without clearing its local receipt";
        complete.act("history/new"); expect(!complete.state.recorded.peek() && !complete.state.reviewed.peek());
        expect(eq(complete.state.title.peek(),before.get("title")->as_string()));
    };

    "off and reduced retain manual real task actions and full protocol detail"_test = [] {
        for (const auto level : {MotionLevel::off,MotionLevel::reduced}) {
            Fixture f; f.rt->set_motion_level(level); f.tick();
            expect(!f.state.running.peek() && !f.state.preview_timer);
            f.act("specimen/reviewed","toggle"); f.act("specimen/record");
            expect(f.state.recorded.peek() && f.state.preview_interactive.peek());
            f.act("history/new"); f.act("modes","set_value",Value{}.set("value","develop")); f.act("method/toggle");
            f.act("develop/requests/assert"); expect(f.state.status.peek().starts_with("FAIL"));
            f.act("develop/requests/change"); f.act("develop/requests/assert");
            expect(f.state.status.peek().starts_with("PASS"));
            Fixture scripted; scripted.rt->set_motion_level(level); scripted.tick();
            scripted.act("use/actions/play"); scripted.advance(2);
            expect(scripted.state.reviewed.peek() && scripted.state.recorded.peek() && !scripted.state.running.peek()) << "an explicitly requested simulation still completes real actions without automatic motion";
        }
    };

    "explicit tasks retain manual authorization across reduced-motion ticks and yield to new input"_test = [] {
        for (const auto level : {MotionLevel::off, MotionLevel::reduced}) {
            Fixture develop("develop"); develop.rt->set_motion_level(level); develop.tick();
            develop.act("preview/actions/verify"); develop.tick();
            expect(develop.state.preview_manual && develop.state.running.peek()) << "a normal coordinator tick preserves explicit authorization";
            develop.advance(1);
            expect(develop.state.status.peek().starts_with("PASS") && develop.state.requests.peek()>=3);
            expect(eq(develop.state.title.peek(),develop.state.request_text.peek()));
            Fixture design("design"); design.rt->set_motion_level(level); design.tick(); const auto data=design.task();
            design.act("preview/actions/arrange"); design.tick();
            expect(design.state.layout.peek()=="cards" && design.task()==data) << "the manual layout action works without animation and preserves task data";
            design.act("preview/actions/replay"); design.tick(); design.advance(1);
            expect(design.state.requests.peek()>=3 && !design.state.running.peek()) << "explicit design replay also completes its finite semantic sequence";
            Fixture interrupted("develop"); interrupted.rt->set_motion_level(level); interrupted.tick();
            interrupted.act("preview/actions/verify"); interrupted.tick();
            interrupted.act("specimen/title","set_value",Value{}.set("value","a newer manual title")); interrupted.advance(1);
            expect(eq(interrupted.state.title.peek(),std::string("a newer manual title")));
            expect(interrupted.state.preview_interactive.peek() && !interrupted.state.preview_timer && !interrupted.state.running.peek());
        }
    };

    "destroying a model with a pending preview leaves no dangling timer access"_test = [] {
        Headless h(HeadlessOptions{.size={640,1400}}); Runtime* rt=&h.runtime();
        auto model=std::make_unique<site::AgentFeature>();
        site::AgentFeatureContext c; c.runtime=&rt; c.root="main/agent_feature";
        h.mount("main",column({.width=640,.padding=16},{site::agent_feature(*model,c,600)})); h.step();
        site::tick_agent_feature(*model,c); expect(model->preview_timer!=0);
        h.mount("main",box({.id="another",.width=640,.height=200})); h.step(); model.reset();
        h.advance(seconds(3));
        expect(std::ranges::none_of(rt->action_journal(),[](const auto& e){return e.actor=="agent:agent-feature-simulated";}));
    };
}};
