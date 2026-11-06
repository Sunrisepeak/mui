import std;
import hanim.engine;
import mlibs.log;
import mlibs.testing;

using namespace hanim;
using namespace mlibs::testing;
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
    suite.test("scopes share atomic frame publication and all playback controls",[] {
        Fixture f;const auto plan=f.engine.prepare(composite(anim::morph(f.a,f.b).with_effect(fx::blur(0,2))).with_effect(fx::blur(0,8)));
        check(plan.has_value());const auto ref=*f.engine.play(*plan);
        check(f.engine.seek_progress(ref,.5).has_value());auto current=f.engine.program_frame(ref);
        check(current->scene.preparation==plan->identity() && current->info.scope_count==1 && current->scene.tracks.size()==1);
        check(current->scene.scope_effect_values[0]==4 && current->scene.tracks[0].effect_values[0]==1);
        check(current->scene.tracks[0].geometry.parts[1].local.tx==20);
        check(f.engine.pause(ref).has_value() && f.engine.advance(seconds(100)).has_value());
        check(f.engine.program_frame(ref)->scene.position==seconds(.5));
        check(f.engine.reverse(ref).has_value() && f.engine.resume(ref).has_value() && f.engine.advance(seconds(.25)).has_value());
        check(f.engine.program_frame(ref)->scene.scope_effect_values[0]==2);
        check(f.engine.retarget(ref,anim::morph_to(f.b)).error().code==EngineError::unsupported_operation);
        check(f.engine.program_frame(ref)->scene.scope_effect_values[0]==2 && f.engine.inspect(ref)->plan_revision==1);
        check(f.engine.cancel(ref,CancelMode::restore).has_value());
        current=f.engine.program_frame(ref);check(current->scene.scope_effect_values[0]==0 && current->scene.tracks[0].effect_values[0]==0);
        check(f.engine.release(ref).has_value() && f.engine.program_frame(ref).error().code==EngineError::stale_handle);
    });
    suite.test("scope storage and effect limits are included in player admission and release",[] {
        Fixture f({.max_total_effect_parameters=1});
        const auto plan=f.engine.prepare(composite(anim::morph(f.a,f.b).with_effect(fx::blur(0,1))).with_effect(fx::blur(0,2)));
        check(plan.has_value() && f.engine.play(*plan).error().code==EngineError::capacity && f.engine.size()==0);
        const auto small=f.engine.prepare(composite(anim::morph(f.a,f.b)).with_effect(fx::blur(0,2)));
        const auto ref=f.engine.play(*small);check(ref.has_value() && f.engine.release(*ref).has_value() && f.engine.play(*small).has_value());
        Fixture limited({.effects={.max_effects=1}});
        const auto oversized=PreparedProgram::prepare(composite(anim::morph(*limited.engine.capture_object(limited.a),
            *limited.engine.capture_object(limited.b))).with_effect(fx::blur(0,1)).with_effect(fx::blur(0,2)));
        check(oversized.has_value() && limited.engine.play(*oversized).error().code==EngineError::effect && limited.engine.size()==0);
    });
    suite.test("failed geometry cannot publish newly sampled scope parameters",[] {
        Fixture f;const auto huge=*f.engine.create_object(shape(false,1e308)),other=*f.engine.create_object(shape(true,-1e308));
        const auto ref=f.engine.play(composite(parallel(anim::morph(f.a,f.b),anim::morph(huge,other))).with_effect(fx::blur(0,8)));
        check(ref.has_value());const auto status=f.engine.advance(seconds(.5));check(!status && status.error().code==EngineError::animation);
        const auto frame=f.engine.program_frame(*ref);check(frame->scene.position==seconds(0) && frame->scene.scope_effect_values[0]==0);
        check(frame->scene.tracks[0].geometry.parts[1].local.tx==0);
        check(f.engine.seek(*ref,seconds(1)).has_value() && f.engine.program_frame(*ref)->scene.scope_effect_values[0]==8);
    });
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
        check(mlibs::log::set_log_filter("hanim.engine=debug,*=warning").has_value());
        mlibs::log::set_log_sink([](void* context,const mlibs::log::LogRecord& record) {
            if (record.category != "hanim.engine" || !record.message.starts_with("pause")) return;
            auto& state = *static_cast<Observer*>(context); ++state.calls;
            state.paused = state.engine->inspect(state.ref)->state == PlaybackState::paused;
            auto nested = state.engine->release(state.ref);
            state.refused = !nested && nested.error().code == EngineError::reentrant_mutation;
        },&observer);
        const auto result = f.engine.pause(ref);
        mlibs::log::set_log_sink(nullptr,nullptr); check(mlibs::log::set_log_filter("*=info").has_value());
        check(result.has_value() && observer.calls == 1 && observer.paused && observer.refused);
        check(f.engine.inspect(ref)->state == PlaybackState::paused);
    });
    suite.test("animation preparation freezes inputs and rejects observer mutation", [] {
        Fixture f;
        struct Observer { Engine* engine; ObjectRef object; int calls=0; bool refused=false, prepare_refused=false; }
            observer{&f.engine,f.b};
        check(mlibs::log::set_log_filter("hanim.program=debug,*=warning").has_value());
        mlibs::log::set_log_sink([](void* context,const mlibs::log::LogRecord& record) {
            if(record.category!="hanim.program")return;
            auto& state=*static_cast<Observer*>(context);++state.calls;
            auto removed=state.engine->destroy_object(state.object);
            state.refused=!removed && removed.error()==ObjectError::reentrant_mutation;
            auto nested=state.engine->prepare(anim::morph(state.object,state.object));
            state.prepare_refused=!nested && nested.error().code==EngineError::reentrant_mutation;
        },&observer);
        const auto plan=f.engine.prepare(anim::morph(f.a,f.b));
        mlibs::log::set_log_sink(nullptr,nullptr);check(mlibs::log::set_log_filter("*=info").has_value());
        check(plan && observer.calls>0 && observer.refused && observer.prepare_refused);
        check(f.engine.size()==0 && f.engine.capture_object(f.b).has_value());
        check(plan->tracks()[0].source_revision==f.engine.capture_object(f.a)->revision());
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
    suite.test("nested sequence and parallel bindings share one controllable clock and explicit frames", [] {
        Fixture f;
        const auto c = *f.engine.create_object(shape(true,80,{0,1,0,1}));
        const auto d = *f.engine.create_object(shape(false,100)), e = *f.engine.create_object(shape(true,140));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        auto program = PreparedProgram::prepare(parallel({
            sequence({bind(a,b,{fx::blur(0,8)}),bind(a,*f.engine.capture_object(c),{fx::blur(8,12)})}),
            bind(*f.engine.capture_object(d),*f.engine.capture_object(e),{}, {.duration=seconds(2)})}));
        check(program.has_value());
        const auto ref = f.engine.play(*program); check(ref.has_value());
        check(f.engine.inspect(*ref)->track_count==2 && f.engine.inspect(*ref)->segment_count==3 && f.engine.inspect(*ref)->duration==seconds(2));
        check(!f.engine.inspect(*ref)->sample_progress.has_value(), "independent tracks have no invented common easing");
        check(f.engine.frame(*ref).error().code==EngineError::ambiguous_frame);
        check(f.engine.frame(*ref,2).error().code==EngineError::invalid_argument);
        std::size_t ai = program->tracks()[0].object==f.a ? 0 : 1, di = 1-ai;
        check(f.engine.seek(*ref,seconds(.5)).has_value());
        check(f.engine.frame(*ref,ai)->geometry.parts[1].local.tx==20 && f.engine.frame(*ref,ai)->effect_values[0]==4);
        check(f.engine.frame(*ref,di)->geometry.parts[1].local.tx==110);
        check(f.engine.seek(*ref,seconds(1.5)).has_value());
        const auto current = f.engine.frame(*ref,ai);
        check(current->geometry.parts[1].local.tx==60 && current->effect_values[0]==10);
        check(current->animation->source_object()==f.a && current->animation->to().object()==c);
        check(f.engine.frame(*ref,di)->geometry.parts[1].local.tx==130);
        check(f.engine.pause(*ref).has_value() && f.engine.advance(seconds(100)).has_value());
        check(f.engine.inspect(*ref)->position==seconds(1.5));
        check(f.engine.reverse(*ref).has_value() && f.engine.resume(*ref).has_value() && f.engine.advance(seconds(1)).has_value());
        check(f.engine.inspect(*ref)->position==seconds(.5) && f.engine.frame(*ref,ai)->geometry.parts[1].local.tx==20);
        check(f.engine.capture_object(f.a)->revision()==1 && f.engine.capture_object(d)->revision()==1);
    });
    suite.test("prepared programs retain all source occupancy and release every track quota", [] {
        Fixture f({.max_playbacks=2,.max_total_parts=4});
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        auto plan = PreparedProgram::prepare(parallel({sequence({bind(a,b),bind(a,b)}),
            bind(*f.engine.capture_object(c),*f.engine.capture_object(d))})); check(plan.has_value());
        const auto ref = f.engine.play(*plan); check(ref.has_value());
        check(f.engine.play(c,anim::morph_to(d)).error().code==EngineError::binding_conflict);
        const auto extra = *f.engine.create_object(shape(false,10));
        check(f.engine.play(extra,anim::morph_to(d)).error().code==EngineError::capacity);
        check(f.engine.cancel(*ref).has_value() && f.engine.play(*plan).error().code==EngineError::binding_conflict);
        check(f.engine.release(*ref).has_value());
        const auto replacement = f.engine.play(*plan); check(replacement.has_value());
        check(replacement->slot==ref->slot && replacement->generation==ref->generation+1);
        check(f.engine.frame(*ref,0).error().code==EngineError::stale_handle);
    });
    suite.test("a later failing program track cannot publish earlier tracks or other playbacks", [] {
        Fixture f; const auto independent = *f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,8));
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        const auto x = *f.engine.create_object(shape(false,1e308)), y = *f.engine.create_object(shape(true,-1e308));
        auto plan = PreparedProgram::prepare(parallel({
            bind(*f.engine.capture_object(c),*f.engine.capture_object(d),{fx::blur(0,4)}),
            bind(*f.engine.capture_object(x),*f.engine.capture_object(y))})); check(plan.has_value());
        const auto ref = f.engine.play(*plan); check(ref.has_value());
        const auto ci = plan->tracks()[0].object==c ? 0u : 1u;
        const auto result = f.engine.advance(seconds(.5));
        check(!result && result.error().code==EngineError::animation && result.error().program.has_value());
        check(f.engine.inspect(independent)->position==seconds(0) && f.engine.inspect(*ref)->position==seconds(0));
        check(f.engine.frame(independent)->effect_values[0]==0 && f.engine.frame(*ref,ci)->effect_values[0]==0);
        check(f.engine.frame(independent)->geometry.parts[1].local.tx==0 && f.engine.frame(*ref,ci)->geometry.parts[1].local.tx==0);
        check(f.engine.seek(*ref,seconds(1)).has_value() && f.engine.advance(seconds(.5)).has_value());
        check(f.engine.inspect(independent)->position==seconds(.5));
    });
    suite.test("single track sequences can retarget their actual phase while multitrack replacement is explicit", [] {
        Fixture f; const auto c = *f.engine.create_object(shape(true,80)), d = *f.engine.create_object(shape(true,120));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        const auto plan = PreparedProgram::prepare(sequence({bind(a,b,{fx::blur(0,8)}),bind(a,*f.engine.capture_object(c),{fx::blur(8,12)})}));
        check(plan.has_value()); const auto ref = *f.engine.play(*plan);
        check(f.engine.seek(ref,seconds(1.5)).has_value() && f.engine.pause(ref).has_value());
        check(f.engine.retarget(ref,anim::morph_to(d),{.easing=Ease::stepped(2,StepPosition::jump_start)}).has_value());
        check(f.engine.frame(ref)->geometry.parts[1].local.tx==60 && f.engine.frame(ref)->effect_values[0]==10);
        check(f.engine.inspect(ref)->segment_count==1 && f.engine.inspect(ref)->plan_revision==2 && f.engine.inspect(ref)->state==PlaybackState::paused);
        check(f.engine.release(ref).has_value());
        const auto multiple = PreparedProgram::prepare(parallel({bind(a,b),bind(*f.engine.capture_object(c),*f.engine.capture_object(d))}));
        const auto multi = *f.engine.play(*multiple);
        check(f.engine.retarget(multi,anim::morph_to(d)).error().code==EngineError::unsupported_operation);
        check(f.engine.inspect(multi)->track_count==2 && f.engine.inspect(multi)->plan_revision==1);
    });
    suite.test("program copies own inputs and playback instances keep separate cursors", [] {
        PreparedProgram plan;
        { Fixture f; const auto d = *f.engine.create_object(shape(false,80)), e = *f.engine.create_object(shape(true,120));
          auto prepared = PreparedProgram::prepare(stagger(seconds(.5),{
              bind(*f.engine.capture_object(f.a),*f.engine.capture_object(f.b)),
              bind(*f.engine.capture_object(d),*f.engine.capture_object(e))})); check(prepared.has_value()); plan=*prepared; }
        auto first = Engine::make(), second = Engine::make();
        const auto one = first->play(plan), two = second->play(plan); check(one.has_value() && two.has_value()); plan={};
        check(first->seek(*one,seconds(1)).has_value() && second->seek(*two,seconds(.25)).has_value());
        check(first->inspect(*one)->position==seconds(1) && second->inspect(*two)->position==seconds(.25));
        check(first->frame(*two,0).error().code==EngineError::foreign_engine);
        check(first->cancel(*one,CancelMode::restore).has_value());
        check(first->frame(*one,0)->geometry.parts[1].local.tx==0 && first->frame(*one,1)->geometry.parts[1].local.tx==80);
    });
    suite.test("prepared program effects cannot bypass the engine's configured per-segment limits", [] {
        Fixture f({.effects={.max_effects=1}});
        const auto plan = PreparedProgram::prepare(bind(*f.engine.capture_object(f.a),*f.engine.capture_object(f.b),
            {fx::blur(0,1),fx::tone({}, {})})); check(plan.has_value());
        const auto refused = f.engine.play(*plan);
        check(!refused && refused.error().code==EngineError::effect && refused.error().effect==EffectError::capacity);
        check(f.engine.size()==0);
        check(f.engine.play(f.a,anim::morph_to(f.b),fx::blur(0,1)).has_value());
    });
    suite.test("phase changes expose actual geometry and effect ranges rather than maximum scratch capacity", [] {
        Fixture f;
        const auto before=*f.engine.capture_object(f.a), target=*f.engine.capture_object(f.b);
        check(f.engine.update_object(f.a,{{{.id="group"}}}).has_value());
        const auto newer=*f.engine.capture_object(f.a);
        const auto other=*f.engine.create_object({{{.id="group",.local={.tx=20}}}});
        const auto plan=PreparedProgram::prepare(sequence({bind(before,target,{fx::blur(0,8)}),
            bind(newer,*f.engine.capture_object(other),{}, {},{},ProgramSource::snapshot)}));
        check(plan.has_value());const auto ref=*f.engine.play(*plan);
        check(f.engine.seek(ref,seconds(.5)).has_value());
        auto frame=f.engine.frame(ref);
        check(frame->geometry.parts.size()==2 && !frame->geometry.cubics.empty() && frame->effect_values.size()==1);
        check(frame->geometry.source_revision==1);
        check(f.engine.seek(ref,seconds(1.5)).has_value());frame=f.engine.frame(ref);
        check(frame->geometry.parts.size()==1 && frame->geometry.cubics.empty() && frame->effect_values.empty());
        check(frame->geometry.parts[0].local.tx==10 && frame->geometry.source_revision==2);
        check(f.engine.seek(ref,seconds(.5)).has_value());frame=f.engine.frame(ref);
        check(frame->geometry.parts.size()==2 && !frame->geometry.cubics.empty() && frame->effect_values.size()==1);
        check(frame->geometry.parts[1].local.tx==20 && frame->effect_values[0]==4);
    });
    suite.test("route storage budgets remain transactional across play retarget and release",[] {
        auto made=Engine::make({.max_total_route_segments=2});check(made.has_value());if(!made)return;
        auto& engine=*made;const auto a=*engine.create_object(shape(false,0)),b=*engine.create_object(shape(false,0));
        MotionPath path;path.line_to({10,0}).line_to({10,10});
        const auto first=engine.play(anim::follow_path(a,path));check(first.has_value());if(!first)return;
        check(engine.seek_progress(*first,.5).has_value());const auto position=engine.frame(*first)->geometry.parts[0].local;
        const auto denied=engine.play(anim::follow_path(b,path));check(!denied && denied.error().code==EngineError::capacity);
        check(engine.frame(*first)->geometry.parts[0].local==position && engine.plan(*first)->route_segment_count()==2);
        check(engine.retarget(*first,PropertyTargets{.position=Vec2{20,0}}).has_value());
        check(engine.frame(*first)->geometry.parts[0].local==position && engine.plan(*first)->route_segment_count()==0);
        const auto second=engine.play(anim::follow_path(b,path));check(second.has_value());if(!second)return;
        check(engine.release(*second).has_value());
        const auto replacement=engine.play(anim::follow_path(b,path));check(replacement.has_value());
        if(replacement)check(engine.release(*replacement).has_value());check(engine.release(*first).has_value());
    });
    return suite.run(argc,argv);
}
