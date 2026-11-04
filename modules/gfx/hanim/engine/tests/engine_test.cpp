import std;
import hanim.engine;
import openmui.log;
import openmui.testing;

using namespace hanim;
using namespace omui::testing;
namespace {
ObjectDescription shape(bool circle, double tx, Rgba color = {1, 0, 0, 1}) {
    constexpr double k = 0.5522847498307936;
    std::vector<ShapeSegment> edges = circle ? std::vector<ShapeSegment>{
        ShapeSegment::cubic({k,-1},{1,-k},{1,0}), ShapeSegment::cubic({1,k},{k,1},{0,1}),
        ShapeSegment::cubic({-k,1},{-1,k},{-1,0}), ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})} :
        std::vector<ShapeSegment>{ShapeSegment::line({-1,1}), ShapeSegment::line({1,1})};
    const auto contour = Contour::copy({{0,-1}, edges, true}); check(contour.has_value());
    return {{{.id="group"}, {.id="outline", .parent="group", .local={.tx=tx}, .contours={*contour}, .fill=color}}};
}
struct Fixture {
    Engine engine;
    ObjectRef a, b;
    Fixture(EngineLimits limits = {}) {
        auto made = Engine::make(limits); check(made.has_value()); engine = std::move(*made);
        a = *engine.create_object(shape(false,0)); b = *engine.create_object(shape(true,40,{0,0,1,1}));
    }
};
}
int main(int argc, char** argv) {
    Suite suite{"hanim.engine"};
    suite.test("the public player samples geometry transform fill and ordered effects on one time", [] {
        Fixture f;
        const auto effects = fx::chain({fx::blur(0,8), fx::tone({}, {.r=1,.g=0,.b=0,.amount=0.5f})});
        const auto ref = f.engine.play(f.a, anim::morph_to(f.b), effects); check(ref.has_value());
        check(f.engine.advance(seconds(0.25)).has_value());
        auto frame = f.engine.frame(*ref); check(frame.has_value());
        check(frame->info.state == PlaybackState::playing && frame->info.progress == 0.25 && frame->geometry.progress == 0.25);
        check(frame->geometry.parts.size() == 2 && frame->geometry.parts[1].info.id == "outline");
        check(frame->geometry.parts[1].local.tx == 10 && frame->geometry.parts[1].fill == std::optional(lerp(Rgba{1,0,0,1},Rgba{0,0,1,1},0.25)));
        check(frame->effect_values.size() == 5 && frame->effect_values[0] == 2 && frame->effects->effects()[0].id == "blur");
        for (const auto& contour : frame->geometry.contours) {
            auto cubics = frame->geometry.cubics.subspan(contour.first_cubic,contour.cubic_count);
            for (std::size_t i=0;i<cubics.size();++i) check(cubics[i].end == cubics[(i+1)%cubics.size()].start);
        }
        const auto original = f.engine.capture_object(f.a);
        check((*original->get(*original->find("outline")))->local.tx == 0,
            "visual sampling never changes the source model");
    });
    suite.test("pause seek reverse rate and finished state preserve deterministic control semantics", [] {
        Fixture f; const auto ref = *f.engine.play(f.a,anim::morph_to(f.b));
        check(f.engine.advance(seconds(0.25)).has_value() && f.engine.pause(ref).has_value());
        check(f.engine.advance(seconds(10)).has_value() && f.engine.inspect(ref)->position == seconds(0.25));
        check(f.engine.seek_progress(ref,0.75).has_value() && f.engine.inspect(ref)->state == PlaybackState::paused);
        check(f.engine.rate(ref,2).has_value() && f.engine.resume(ref).has_value());
        check(f.engine.advance(seconds(0.125)).has_value() && f.engine.inspect(ref)->state == PlaybackState::finished);
        check(f.engine.reverse(ref).has_value() && f.engine.inspect(ref)->state == PlaybackState::playing);
        check(f.engine.advance(seconds(0.25)).has_value() && f.engine.inspect(ref)->progress == 0.5);
        check(f.engine.rate(ref,-std::numeric_limits<double>::max()).has_value());
        check(f.engine.advance(seconds(std::numeric_limits<double>::max())).has_value());
        check(f.engine.inspect(ref)->position == seconds(0) && f.engine.inspect(ref)->state == PlaybackState::finished);
        check(f.engine.seek(ref,seconds(-100)).has_value() && f.engine.inspect(ref)->progress == 0);
        check(f.engine.seek(ref,seconds(100)).has_value() && f.engine.inspect(ref)->progress == 1);
    });
    suite.test("retarget starts at current pose and effect values without changing business objects", [] {
        Fixture f; const auto ref = *f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,8));
        check(f.engine.seek_progress(ref,0.5).has_value() && f.engine.pause(ref).has_value());
        const auto before = f.engine.frame(ref);
        const auto local = before->geometry.parts[1].local; const auto fill = before->geometry.parts[1].fill;
        const auto point = before->geometry.cubics.front().start; const auto effect = before->effect_values[0];
        const auto witness = before->geometry.preparation;
        const auto c = *f.engine.create_object(shape(true,100,{0,1,0,1}));
        check(f.engine.retarget(ref,anim::morph_to(c),{.duration=seconds(2)}).has_value());
        const auto now = f.engine.frame(ref);
        check(now->info.plan_revision == 2 && now->info.state == PlaybackState::paused && now->info.progress == 0);
        check(now->geometry.preparation != witness && now->geometry.source_object == f.a);
        check(now->geometry.parts[1].local == local && now->geometry.parts[1].fill == fill && now->geometry.cubics.front().start == point);
        check(now->effect_values[0] == effect && now->animation->from().parts().empty());
        check(f.engine.capture_object(f.a)->revision() == 1 && f.engine.capture_object(c)->revision() == 1);
        check(f.engine.resume(ref).has_value() && f.engine.advance(seconds(2)).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 100 && f.engine.frame(ref)->effect_values[0] == 8);
    });
    suite.test("a failed later sample cannot partially publish an earlier playback in the batch", [] {
        Fixture f; const auto good = *f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,8));
        const auto a = *f.engine.create_object(shape(false,1e308)), b = *f.engine.create_object(shape(true,-1e308));
        const auto bad = f.engine.play(a,anim::morph_to(b)); check(bad.has_value());
        const auto current = f.engine.frame(good);
        const auto local = current->geometry.parts[1].local;
        const auto fill = current->geometry.parts[1].fill;
        const std::vector<ShapeCubic> before(current->geometry.cubics.begin(),current->geometry.cubics.end());
        const auto effect = current->effect_values[0];
        const auto result = f.engine.advance(seconds(0.5));
        check(!result && result.error().code == EngineError::animation);
        check(f.engine.inspect(good)->position == seconds(0) && f.engine.inspect(*bad)->position == seconds(0));
        const auto after = f.engine.frame(good);
        check(after->geometry.parts[1].local == local && after->geometry.parts[1].fill == fill && after->effect_values[0] == effect);
        check(after->geometry.cubics.size() == before.size());
        for (std::size_t i=0;i<before.size();++i) {
            const auto& a=after->geometry.cubics[i]; const auto& b=before[i];
            check(a.start==b.start && a.control1==b.control1 && a.control2==b.control2 && a.end==b.end);
        }
        check(f.engine.seek(*bad,seconds(1)).has_value() && f.engine.advance(seconds(0.5)).has_value());
        check(f.engine.inspect(good)->position == seconds(0.5),"a refused batch consumes no elapsed time");
    });
    suite.test("failed preparations preserve the current frame and pooled capacity", [] {
        Fixture f({.max_playbacks=1});
        auto invalid = shape(true,40); invalid.parts[1].id = "other";
        auto other = *f.engine.create_object(invalid);
        auto failed = f.engine.play(f.a,anim::morph_to(other)); check(!failed && failed.error().code == EngineError::animation);
        const auto ref = *f.engine.play(f.a,anim::morph_to(f.b)); check(ref.slot == 0 && ref.generation == 1);
        check(f.engine.seek_progress(ref,0.5).has_value());
        const auto witness = f.engine.frame(ref)->geometry.preparation; const auto info = *f.engine.inspect(ref);
        check(!f.engine.retarget(ref,anim::morph_to(other)));
        check(f.engine.frame(ref)->geometry.preparation == witness && f.engine.inspect(ref)->plan_revision == info.plan_revision && f.engine.inspect(ref)->position == info.position);
        check(f.engine.play(f.a,anim::morph_to(f.b)).error().code == EngineError::binding_conflict);
        check(f.engine.release(ref).has_value());
        const auto replacement = *f.engine.play(f.a,anim::morph_to(f.b));
        check(replacement.slot == ref.slot && replacement.generation == ref.generation+1);
        check(f.engine.frame(ref).error().code == EngineError::stale_handle);
    });
    suite.test("external snapshots remain playable after their original host disappears", [] {
        Snapshot a,b;
        { auto host = ObjectRegistry::make(); const auto ar = host->create(shape(false,0)), br = host->create(shape(true,40));
          a = *host->capture(*ar); b = *host->capture(*br); }
        auto engine = Engine::make(); const auto ref = engine->play(a,b); check(ref.has_value());
        a = {}; b = {};
        check(engine->seek_progress(*ref,0.5).has_value() && engine->frame(*ref)->geometry.parts[1].local.tx == 20);
        Engine moved = std::move(*engine);
        check(engine->owner() == 0 && moved.frame(*ref).has_value());
        auto foreign = Engine::make(); check(foreign->inspect(*ref).error().code == EngineError::foreign_engine);
        std::array<PlaybackInfo,1> items; check(moved.list(items) == 1 && items[0].ref == *ref);
    });
    suite.test("cancellation keeps explicit hold or restore semantics including instant transitions", [] {
        Fixture f; auto ref = *f.engine.play(f.a,anim::morph_to(f.b));
        check(f.engine.seek_progress(ref,0.5).has_value() && f.engine.cancel(ref).has_value());
        check(f.engine.inspect(ref)->state == PlaybackState::cancelled && f.engine.inspect(ref)->progress == 0.5);
        check(f.engine.advance(seconds(1)).has_value() && f.engine.inspect(ref)->progress == 0.5);
        check(f.engine.resume(ref).error().code == EngineError::cancelled);
        check(f.engine.play(f.a,anim::morph_to(f.b)).error().code == EngineError::binding_conflict);
        check(f.engine.release(ref).has_value());
        ref = *f.engine.play(f.a,anim::morph_to(f.b),Effects{}, {.duration=seconds(0)});
        check(f.engine.inspect(ref)->state == PlaybackState::finished && f.engine.inspect(ref)->progress == 1);
        check(f.engine.cancel(ref,CancelMode::restore).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 0 && f.engine.inspect(ref)->progress == 0);
        check(f.engine.play(f.a,anim::morph_to(f.b)).error().code == EngineError::binding_conflict);
        check(f.engine.capture_object(f.b)->revision() == 1);
    });
    suite.test("invalid time and easing reject without changing a visible frame", [] {
        Fixture f; auto ref = *f.engine.play(f.a,anim::morph_to(f.b));
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        check(f.engine.seek_progress(ref,nan).error().code == EngineError::invalid_time);
        check(f.engine.rate(ref,0).error().code == EngineError::invalid_time);
        check(!f.engine.advance(seconds(-1)) && !f.engine.advance(seconds(nan)));
        check(f.engine.inspect(ref)->progress == 0);
        auto bad = Ease::stepped(2); bad.p[0] = 1e300;
        check(f.engine.retarget(ref,anim::morph_to(f.b),{.easing=bad}).error().code == EngineError::invalid_ease);
        check(f.engine.inspect(ref)->plan_revision == 1);
        check(!Engine::make({.max_playbacks=0}));
        Engine absent; check(absent.advance(seconds(0)).error().code == EngineError::uninitialized);
    });
    suite.test("global retained budgets count bindings and release restores usable capacity", [] {
        Fixture f({.max_playbacks=2,.max_total_parts=2,.max_total_effect_parameters=0});
        check(f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,1)).error().code == EngineError::capacity);
        const auto first = *f.engine.play(f.a,anim::morph_to(f.b)); check(first.slot == 0 && first.generation == 1);
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        check(f.engine.play(c,anim::morph_to(d)).error().code == EngineError::capacity && f.engine.size() == 1);
        check(f.engine.list({}).error().code == EngineError::capacity && f.engine.inspect(first).has_value());
        check(f.engine.release(first).has_value());
        const auto next = f.engine.play(c,anim::morph_to(d)); check(next.has_value());
        check(next->slot == first.slot && next->generation == first.generation+1);
    });
    suite.test("transform-only objects need no contour or effect allocation quota", [] {
        auto engine = Engine::make({.max_total_cubics=0,.max_total_effect_parameters=0}); check(engine.has_value());
        const auto a = *engine->create_object({{{.id="group"}}});
        const auto b = *engine->create_object({{{.id="group",.local={.tx=20}}}});
        const auto ref = engine->play(a,anim::morph_to(b)); check(ref.has_value());
        check(engine->advance(seconds(0.25)).has_value());
        const auto frame = engine->frame(*ref);
        check(frame->geometry.parts[0].local.tx == 5 && frame->geometry.cubics.empty() && frame->effect_values.empty());
    });
    suite.test("log observers see committed controls and cannot reenter mutation", [] {
        Fixture f; const auto ref = *f.engine.play(f.a,anim::morph_to(f.b));
        struct Observer { Engine* engine; PlaybackRef ref; int calls=0; bool paused=false,refused=false; } observer{&f.engine,ref};
        check(omui::set_log_filter("hanim.engine=debug,*=warning").has_value());
        omui::set_log_sink([](void* context,const omui::LogRecord& record) {
            if (record.category != "hanim.engine" || !record.message.starts_with("pause")) return;
            auto& state = *static_cast<Observer*>(context); ++state.calls;
            state.paused = state.engine->inspect(state.ref)->state == PlaybackState::paused;
            auto nested = state.engine->release(state.ref);
            state.refused = !nested && nested.error().code == EngineError::reentrant_mutation;
        },&observer);
        const auto result = f.engine.pause(ref);
        omui::set_log_sink(nullptr,nullptr); check(omui::set_log_filter("*=info").has_value());
        check(result.has_value() && observer.calls == 1 && observer.paused && observer.refused);
        check(f.engine.inspect(ref)->state == PlaybackState::paused);
    });
    suite.test("steps retain their ordinary start but restore and retarget anchor the exact source", [] {
        Fixture f;
        const auto step = Ease::stepped(2,StepPosition::jump_start);
        auto ref = *f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,8),{.easing=step});
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 20,"ordinary play keeps core jump-start semantics");
        check(f.engine.cancel(ref,CancelMode::restore).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 0 && f.engine.frame(ref)->effect_values[0] == 0);
        check(f.engine.release(ref).has_value());
        ref = *f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,8));
        check(f.engine.seek_progress(ref,0.25).has_value());
        const auto c = *f.engine.create_object(shape(true,80));
        check(f.engine.retarget(ref,anim::morph_to(c),{.easing=step}).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 10 && f.engine.frame(ref)->effect_values[0] == 2,
            "retarget's exact start is the old current pose, even for a jump-start curve");
        check(f.engine.seek_progress(ref,0.01).has_value() && f.engine.frame(ref)->geometry.parts[1].local.tx == 45);
        check(f.engine.seek_progress(ref,0).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx == 10 && f.engine.frame(ref)->effect_values[0] == 2,
            "arbitrary seek to the segment start reproduces the same anchored pose");
    });
    return suite.run(argc,argv);
}
