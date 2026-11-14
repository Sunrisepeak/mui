import std;
import hanim.engine;
import mlibs.log;
import mcpplibs.mtest;

using namespace hanim;
using namespace mcpplibs::mtest;
namespace {
ObjectDescription shape(bool circle, double tx, Rgba color = {1, 0, 0, 1}) {
    constexpr double k = 0.5522847498307936;
    std::vector<ShapeSegment> edges = circle ? std::vector<ShapeSegment>{
        ShapeSegment::cubic({k,-1},{1,-k},{1,0}), ShapeSegment::cubic({1,k},{k,1},{0,1}),
        ShapeSegment::cubic({-k,1},{-1,k},{-1,0}), ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})} :
        std::vector<ShapeSegment>{ShapeSegment::line({-1,1}), ShapeSegment::line({1,1})};
    const auto contour = Contour::copy({{0,-1}, edges, true}); expect(contour.has_value());
    return {{{.id="group"}, {.id="outline", .parent="group", .local={.tx=tx}, .contours={*contour}, .fill=color}}};
}
struct Fixture {
    Engine engine;
    ObjectRef a, b;
    Fixture(EngineLimits limits = {}) {
        auto made = Engine::make(limits); expect(made.has_value()); engine = std::move(*made);
        a = *engine.create_object(shape(false,0)); b = *engine.create_object(shape(true,40,{0,0,1,1}));
    }
};
}
UTest cases{"hanim.engine", [] {
    "scopes share atomic frame publication and all playback controls"_test = [] {
        Fixture f;const auto plan=f.engine.prepare(composite(anim::morph(f.a,f.b).with_effect(fx::blur(0,2))).with_effect(fx::blur(0,8)));
        expect(plan.has_value());const auto ref=*f.engine.play(*plan);
        expect(f.engine.seek_progress(ref,.5).has_value());auto current=f.engine.program_frame(ref);
        expect(current->scene.preparation==plan->identity() && current->info.scope_count==1 && current->scene.tracks.size()==1);
        expect(current->scene.scope_effect_values[0]==4 && current->scene.tracks[0].effect_values[0]==1);
        expect(current->scene.tracks[0].geometry.parts[1].local.tx==20);
        expect(f.engine.pause(ref).has_value() && f.engine.advance(seconds(100)).has_value());
        expect(f.engine.program_frame(ref)->scene.position==seconds(.5));
        expect(f.engine.reverse(ref).has_value() && f.engine.resume(ref).has_value() && f.engine.advance(seconds(.25)).has_value());
        expect(f.engine.program_frame(ref)->scene.scope_effect_values[0]==2);
        expect(f.engine.retarget(ref, *f.engine.capture_object(f.b)).error().code==EngineError::unsupported_operation);
        expect(f.engine.program_frame(ref)->scene.scope_effect_values[0]==2 && f.engine.inspect(ref)->plan_revision==1);
        expect(f.engine.cancel(ref,CancelMode::restore).has_value());
        current=f.engine.program_frame(ref);expect(current->scene.scope_effect_values[0]==0 && current->scene.tracks[0].effect_values[0]==0);
        expect(f.engine.release(ref).has_value() && f.engine.program_frame(ref).error().code==EngineError::stale_handle);
    };
    "scope storage and effect limits are included in player admission and release"_test = [] {
        Fixture f({.max_total_effect_parameters=1});
        const auto plan=f.engine.prepare(composite(anim::morph(f.a,f.b).with_effect(fx::blur(0,1))).with_effect(fx::blur(0,2)));
        expect(plan.has_value() && f.engine.play(*plan).error().code==EngineError::capacity && f.engine.size()==0);
        const auto small=f.engine.prepare(composite(anim::morph(f.a,f.b)).with_effect(fx::blur(0,2)));
        const auto ref=f.engine.play(*small);expect(ref.has_value() && f.engine.release(*ref).has_value() && f.engine.play(*small).has_value());
        Fixture limited({.effects={.max_effects=1}});
        const auto oversized=PreparedProgram::prepare(composite(anim::morph(*limited.engine.capture_object(limited.a),
            *limited.engine.capture_object(limited.b))).with_effect(fx::blur(0,1)).with_effect(fx::blur(0,2)));
        expect(oversized.has_value() && limited.engine.play(*oversized).error().code==EngineError::effect && limited.engine.size()==0);
    };
    "failed geometry cannot publish newly sampled scope parameters"_test = [] {
        Fixture f;const auto huge=*f.engine.create_object(shape(false,1e308)),other=*f.engine.create_object(shape(true,-1e308));
        const auto ref=f.engine.play(composite(parallel(anim::morph(f.a,f.b),anim::morph(huge,other))).with_effect(fx::blur(0,8)));
        expect(ref.has_value());const auto status=f.engine.advance(seconds(.5));expect(!status && status.error().code==EngineError::animation);
        const auto frame=f.engine.program_frame(*ref);expect(frame->scene.position==seconds(0) && frame->scene.scope_effect_values[0]==0);
        expect(frame->scene.tracks[0].geometry.parts[1].local.tx==0);
        expect(f.engine.seek(*ref,seconds(1)).has_value() && f.engine.program_frame(*ref)->scene.scope_effect_values[0]==8);
    };
    "the public player samples geometry transform fill and ordered effects on one time"_test = [] {
        Fixture f;
        const auto effects = fx::chain({fx::blur(0,8), fx::tone({}, {.r=1,.g=0,.b=0,.amount=0.5f})});
        const auto ref = f.engine.play(anim::morph(f.a, f.b).with_effects(effects)); expect(ref.has_value());
        expect(f.engine.advance(seconds(0.25)).has_value());
        auto frame = f.engine.frame(*ref); expect(frame.has_value());
        expect(frame->info.state == PlaybackState::playing && frame->info.progress == 0.25 && frame->geometry.progress == 0.25);
        expect(frame->geometry.parts.size() == 2 && frame->geometry.parts[1].info.id == "outline");
        expect(frame->geometry.parts[1].local.tx == 10 && frame->geometry.parts[1].fill == std::optional(lerp(Rgba{1,0,0,1},Rgba{0,0,1,1},0.25)));
        expect(frame->effect_values.size() == 5 && frame->effect_values[0] == 2 && frame->effects->effects()[0].id == "blur");
        for (const auto& contour : frame->geometry.contours) {
            auto cubics = frame->geometry.cubics.subspan(contour.first_cubic,contour.cubic_count);
            for (std::size_t i=0;i<cubics.size();++i) expect(cubics[i].end == cubics[(i+1)%cubics.size()].start);
        }
        const auto original = f.engine.capture_object(f.a);
        expect((*original->get(*original->find("outline")))->local.tx == 0) << "visual sampling never changes the source model";
    };
    "pause seek reverse rate and finished state preserve deterministic control semantics"_test = [] {
        Fixture f; const auto ref = *f.engine.play(anim::morph(f.a, f.b));
        expect(f.engine.advance(seconds(0.25)).has_value() && f.engine.pause(ref).has_value());
        expect(f.engine.advance(seconds(10)).has_value() && f.engine.inspect(ref)->position == seconds(0.25));
        expect(f.engine.seek_progress(ref,0.75).has_value() && f.engine.inspect(ref)->state == PlaybackState::paused);
        expect(f.engine.rate(ref,2).has_value() && f.engine.resume(ref).has_value());
        expect(f.engine.advance(seconds(0.125)).has_value() && f.engine.inspect(ref)->state == PlaybackState::finished);
        expect(f.engine.reverse(ref).has_value() && f.engine.inspect(ref)->state == PlaybackState::playing);
        expect(f.engine.advance(seconds(0.25)).has_value() && f.engine.inspect(ref)->progress == 0.5);
        expect(f.engine.rate(ref,-std::numeric_limits<double>::max()).has_value());
        expect(f.engine.advance(seconds(std::numeric_limits<double>::max())).has_value());
        expect(f.engine.inspect(ref)->position == seconds(0) && f.engine.inspect(ref)->state == PlaybackState::finished);
        expect(f.engine.seek(ref,seconds(-100)).has_value() && f.engine.inspect(ref)->progress == 0);
        expect(f.engine.seek(ref,seconds(100)).has_value() && f.engine.inspect(ref)->progress == 1);
    };
    "retarget starts at current pose and effect values without changing business objects"_test = [] {
        Fixture f; const auto ref = *f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,8)));
        expect(f.engine.seek_progress(ref,0.5).has_value() && f.engine.pause(ref).has_value());
        const auto before = f.engine.frame(ref);
        const auto local = before->geometry.parts[1].local; const auto fill = before->geometry.parts[1].fill;
        const auto point = before->geometry.cubics.front().start; const auto effect = before->effect_values[0];
        const auto witness = before->geometry.preparation;
        const auto c = *f.engine.create_object(shape(true,100,{0,1,0,1}));
        expect(f.engine.retarget(ref, *f.engine.capture_object(c), {.duration=seconds(2)}).has_value());
        const auto now = f.engine.frame(ref);
        expect(now->info.plan_revision == 2 && now->info.state == PlaybackState::paused && now->info.progress == 0);
        expect(now->geometry.preparation != witness && now->geometry.source_object == f.a);
        expect(now->geometry.parts[1].local == local && now->geometry.parts[1].fill == fill && now->geometry.cubics.front().start == point);
        expect(now->effect_values[0] == effect && now->animation->from().parts().empty());
        expect(f.engine.capture_object(f.a)->revision() == 1 && f.engine.capture_object(c)->revision() == 1);
        expect(f.engine.resume(ref).has_value() && f.engine.advance(seconds(2)).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 100 && f.engine.frame(ref)->effect_values[0] == 8);
    };
    "a failed later sample cannot partially publish an earlier playback in the batch"_test = [] {
        Fixture f; const auto good = *f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,8)));
        const auto a = *f.engine.create_object(shape(false,1e308)), b = *f.engine.create_object(shape(true,-1e308));
        const auto bad = f.engine.play(anim::morph(a, b)); expect(bad.has_value());
        const auto current = f.engine.frame(good);
        const auto local = current->geometry.parts[1].local;
        const auto fill = current->geometry.parts[1].fill;
        const std::vector<ShapeCubic> before(current->geometry.cubics.begin(),current->geometry.cubics.end());
        const auto effect = current->effect_values[0];
        const auto result = f.engine.advance(seconds(0.5));
        expect(!result && result.error().code == EngineError::animation);
        expect(f.engine.inspect(good)->position == seconds(0) && f.engine.inspect(*bad)->position == seconds(0));
        const auto after = f.engine.frame(good);
        expect(after->geometry.parts[1].local == local && after->geometry.parts[1].fill == fill && after->effect_values[0] == effect);
        expect(after->geometry.cubics.size() == before.size());
        for (std::size_t i=0;i<before.size();++i) {
            const auto& a=after->geometry.cubics[i]; const auto& b=before[i];
            expect(a.start==b.start && a.control1==b.control1 && a.control2==b.control2 && a.end==b.end);
        }
        expect(f.engine.seek(*bad,seconds(1)).has_value() && f.engine.advance(seconds(0.5)).has_value());
        expect(f.engine.inspect(good)->position == seconds(0.5)) << "a refused batch consumes no elapsed time";
    };
    "failed preparations preserve the current frame and pooled capacity"_test = [] {
        Fixture f({.max_playbacks=1});
        auto invalid = shape(true,40); invalid.parts[1].id = "other";
        auto other = *f.engine.create_object(invalid);
        auto failed = f.engine.play(anim::morph(f.a, other)); expect(!failed && failed.error().code == EngineError::animation);
        const auto ref = *f.engine.play(anim::morph(f.a, f.b)); expect(ref.slot == 0 && ref.generation == 1);
        expect(f.engine.seek_progress(ref,0.5).has_value());
        const auto witness = f.engine.frame(ref)->geometry.preparation; const auto info = *f.engine.inspect(ref);
        expect(!f.engine.retarget(ref, *f.engine.capture_object(other)));
        expect(f.engine.frame(ref)->geometry.preparation == witness && f.engine.inspect(ref)->plan_revision == info.plan_revision && f.engine.inspect(ref)->position == info.position);
        expect(f.engine.play(anim::morph(f.a, f.b)).error().code == EngineError::binding_conflict);
        expect(f.engine.release(ref).has_value());
        const auto replacement = *f.engine.play(anim::morph(f.a, f.b));
        expect(replacement.slot == ref.slot && replacement.generation == ref.generation+1);
        expect(f.engine.frame(ref).error().code == EngineError::stale_handle);
    };
    "external snapshots remain playable after their original host disappears"_test = [] {
        Snapshot a,b;
        { auto host = ObjectRegistry::make(); const auto ar = host->create(shape(false,0)), br = host->create(shape(true,40));
          a = *host->capture(*ar); b = *host->capture(*br); }
        auto engine = Engine::make(); const auto ref = engine->play(bind(a,b)); expect(ref.has_value());
        a = {}; b = {};
        expect(engine->seek_progress(*ref,0.5).has_value() && engine->frame(*ref)->geometry.parts[1].local.tx == 20);
        Engine moved = std::move(*engine);
        expect(engine->owner() == 0 && moved.frame(*ref).has_value());
        auto foreign = Engine::make(); expect(foreign->inspect(*ref).error().code == EngineError::foreign_engine);
        std::array<PlaybackInfo,1> items; expect(moved.list(items) == 1 && items[0].ref == *ref);
    };
    "cancellation keeps explicit hold or restore semantics including instant transitions"_test = [] {
        Fixture f; auto ref = *f.engine.play(anim::morph(f.a, f.b));
        expect(f.engine.seek_progress(ref,0.5).has_value() && f.engine.cancel(ref).has_value());
        expect(f.engine.inspect(ref)->state == PlaybackState::cancelled && f.engine.inspect(ref)->progress == 0.5);
        expect(f.engine.advance(seconds(1)).has_value() && f.engine.inspect(ref)->progress == 0.5);
        expect(f.engine.resume(ref).error().code == EngineError::cancelled);
        expect(f.engine.play(anim::morph(f.a, f.b)).error().code == EngineError::binding_conflict);
        expect(f.engine.release(ref).has_value());
        ref = *f.engine.play(anim::morph(f.a, f.b).with_effects(Effects{}).options({.duration=seconds(0)}));
        expect(f.engine.inspect(ref)->state == PlaybackState::finished && f.engine.inspect(ref)->progress == 1);
        expect(f.engine.cancel(ref,CancelMode::restore).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 0 && f.engine.inspect(ref)->progress == 0);
        expect(f.engine.play(anim::morph(f.a, f.b)).error().code == EngineError::binding_conflict);
        expect(f.engine.capture_object(f.b)->revision() == 1);
    };
    "invalid time and easing reject without changing a visible frame"_test = [] {
        Fixture f; auto ref = *f.engine.play(anim::morph(f.a, f.b));
        const auto nan = std::numeric_limits<double>::quiet_NaN();
        expect(f.engine.seek_progress(ref,nan).error().code == EngineError::invalid_time);
        expect(f.engine.rate(ref,0).error().code == EngineError::invalid_time);
        expect(!f.engine.advance(seconds(-1)) && !f.engine.advance(seconds(nan)));
        expect(f.engine.inspect(ref)->progress == 0);
        auto bad = Ease::stepped(2); bad.p[0] = 1e300;
        expect(f.engine.retarget(ref, *f.engine.capture_object(f.b), {.easing=bad}).error().code == EngineError::invalid_ease);
        expect(f.engine.inspect(ref)->plan_revision == 1);
        expect(!Engine::make({.max_playbacks=0}));
        Engine absent; expect(absent.advance(seconds(0)).error().code == EngineError::uninitialized);
    };
    "global retained budgets count bindings and release restores usable capacity"_test = [] {
        Fixture f({.max_playbacks=2,.max_total_parts=2,.max_total_effect_parameters=0});
        expect(f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,1))).error().code == EngineError::capacity);
        const auto first = *f.engine.play(anim::morph(f.a, f.b)); expect(first.slot == 0 && first.generation == 1);
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        expect(f.engine.play(anim::morph(c, d)).error().code == EngineError::capacity && f.engine.size() == 1);
        expect(f.engine.list({}).error().code == EngineError::capacity && f.engine.inspect(first).has_value());
        expect(f.engine.release(first).has_value());
        const auto next = f.engine.play(anim::morph(c, d)); expect(next.has_value());
        expect(next->slot == first.slot && next->generation == first.generation+1);
    };
    "transform-only objects need no contour or effect allocation quota"_test = [] {
        auto engine = Engine::make({.max_total_cubics=0,.max_total_effect_parameters=0}); expect(engine.has_value());
        const auto a = *engine->create_object({{{.id="group"}}});
        const auto b = *engine->create_object({{{.id="group",.local={.tx=20}}}});
        const auto ref = engine->play(anim::morph(a, b)); expect(ref.has_value());
        expect(engine->advance(seconds(0.25)).has_value());
        const auto frame = engine->frame(*ref);
        expect(frame->geometry.parts[0].local.tx == 5 && frame->geometry.cubics.empty() && frame->effect_values.empty());
    };
    "log observers see committed controls and cannot reenter mutation"_test = [] {
        Fixture f; const auto ref = *f.engine.play(anim::morph(f.a, f.b));
        struct Observer { Engine* engine; PlaybackRef ref; int calls=0; bool paused=false,refused=false; } observer{&f.engine,ref};
        expect(mlibs::log::set_log_filter("hanim.engine=debug,*=warning").has_value());
        mlibs::log::set_log_sink([](void* context,const mlibs::log::LogRecord& record) {
            if (record.category != "hanim.engine" || !record.message.starts_with("pause")) return;
            auto& state = *static_cast<Observer*>(context); ++state.calls;
            state.paused = state.engine->inspect(state.ref)->state == PlaybackState::paused;
            auto nested = state.engine->release(state.ref);
            state.refused = !nested && nested.error().code == EngineError::reentrant_mutation;
        },&observer);
        const auto result = f.engine.pause(ref);
        mlibs::log::set_log_sink(nullptr,nullptr); expect(mlibs::log::set_log_filter("*=info").has_value());
        expect(result.has_value() && observer.calls == 1 && observer.paused && observer.refused);
        expect(f.engine.inspect(ref)->state == PlaybackState::paused);
    };
    "animation preparation freezes inputs and rejects observer mutation"_test = [] {
        Fixture f;
        struct Observer { Engine* engine; ObjectRef object; int calls=0; bool refused=false, prepare_refused=false; }
            observer{&f.engine,f.b};
        expect(mlibs::log::set_log_filter("hanim.program=debug,*=warning").has_value());
        mlibs::log::set_log_sink([](void* context,const mlibs::log::LogRecord& record) {
            if(record.category!="hanim.program")return;
            auto& state=*static_cast<Observer*>(context);++state.calls;
            auto removed=state.engine->destroy_object(state.object);
            state.refused=!removed && removed.error()==ObjectError::reentrant_mutation;
            auto nested=state.engine->prepare(anim::morph(state.object,state.object));
            state.prepare_refused=!nested && nested.error().code==EngineError::reentrant_mutation;
        },&observer);
        const auto plan=f.engine.prepare(anim::morph(f.a,f.b));
        mlibs::log::set_log_sink(nullptr,nullptr);expect(mlibs::log::set_log_filter("*=info").has_value());
        expect(plan && observer.calls>0 && observer.refused && observer.prepare_refused);
        expect(f.engine.size()==0 && f.engine.capture_object(f.b).has_value());
        expect(plan->tracks()[0].source_revision==f.engine.capture_object(f.a)->revision());
    };
    "steps retain their ordinary start but restore and retarget anchor the exact source"_test = [] {
        Fixture f;
        const auto step = Ease::stepped(2,StepPosition::jump_start);
        auto ref = *f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,8)).options({.easing=step}));
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 20) << "ordinary play keeps core jump-start semantics";
        expect(f.engine.cancel(ref,CancelMode::restore).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 0 && f.engine.frame(ref)->effect_values[0] == 0);
        expect(f.engine.release(ref).has_value());
        ref = *f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,8)));
        expect(f.engine.seek_progress(ref,0.25).has_value());
        const auto c = *f.engine.create_object(shape(true,80));
        expect(f.engine.retarget(ref, *f.engine.capture_object(c), {.easing=step}).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 10 && f.engine.frame(ref)->effect_values[0] == 2) << "retarget's exact start is the old current pose, even for a jump-start curve";
        expect(f.engine.seek_progress(ref,0.01).has_value() && f.engine.frame(ref)->geometry.parts[1].local.tx == 45);
        expect(f.engine.seek_progress(ref,0).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx == 10 && f.engine.frame(ref)->effect_values[0] == 2) << "arbitrary seek to the segment start reproduces the same anchored pose";
    };
    "nested sequence and parallel bindings share one controllable clock and explicit frames"_test = [] {
        Fixture f;
        const auto c = *f.engine.create_object(shape(true,80,{0,1,0,1}));
        const auto d = *f.engine.create_object(shape(false,100)), e = *f.engine.create_object(shape(true,140));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        auto program = PreparedProgram::prepare(parallel({
            sequence({bind(a,b,{fx::blur(0,8)}),bind(a,*f.engine.capture_object(c),{fx::blur(8,12)})}),
            bind(*f.engine.capture_object(d),*f.engine.capture_object(e),{}, {.duration=seconds(2)})}));
        expect(program.has_value());
        const auto ref = f.engine.play(*program); expect(ref.has_value());
        expect(f.engine.inspect(*ref)->track_count==2 && f.engine.inspect(*ref)->segment_count==3 && f.engine.inspect(*ref)->duration==seconds(2));
        expect(!f.engine.inspect(*ref)->sample_progress.has_value()) << "independent tracks have no invented common easing";
        expect(f.engine.frame(*ref).error().code==EngineError::ambiguous_frame);
        expect(f.engine.frame(*ref,2).error().code==EngineError::invalid_argument);
        std::size_t ai = program->tracks()[0].object==f.a ? 0 : 1, di = 1-ai;
        expect(f.engine.seek(*ref,seconds(.5)).has_value());
        expect(f.engine.frame(*ref,ai)->geometry.parts[1].local.tx==20 && f.engine.frame(*ref,ai)->effect_values[0]==4);
        expect(f.engine.frame(*ref,di)->geometry.parts[1].local.tx==110);
        expect(f.engine.seek(*ref,seconds(1.5)).has_value());
        const auto current = f.engine.frame(*ref,ai);
        expect(current->geometry.parts[1].local.tx==60 && current->effect_values[0]==10);
        expect(current->animation->source_object()==f.a && current->animation->to().object()==c);
        expect(f.engine.frame(*ref,di)->geometry.parts[1].local.tx==130);
        expect(f.engine.pause(*ref).has_value() && f.engine.advance(seconds(100)).has_value());
        expect(f.engine.inspect(*ref)->position==seconds(1.5));
        expect(f.engine.reverse(*ref).has_value() && f.engine.resume(*ref).has_value() && f.engine.advance(seconds(1)).has_value());
        expect(f.engine.inspect(*ref)->position==seconds(.5) && f.engine.frame(*ref,ai)->geometry.parts[1].local.tx==20);
        expect(f.engine.capture_object(f.a)->revision()==1 && f.engine.capture_object(d)->revision()==1);
    };
    "prepared programs retain all source occupancy and release every track quota"_test = [] {
        Fixture f({.max_playbacks=2,.max_total_parts=4});
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        auto plan = PreparedProgram::prepare(parallel({sequence({bind(a,b),bind(a,b)}),
            bind(*f.engine.capture_object(c),*f.engine.capture_object(d))})); expect(plan.has_value());
        const auto ref = f.engine.play(*plan); expect(ref.has_value());
        expect(f.engine.play(anim::morph(c, d)).error().code==EngineError::binding_conflict);
        const auto extra = *f.engine.create_object(shape(false,10));
        expect(f.engine.play(anim::morph(extra, d)).error().code==EngineError::capacity);
        expect(f.engine.cancel(*ref).has_value() && f.engine.play(*plan).error().code==EngineError::binding_conflict);
        expect(f.engine.release(*ref).has_value());
        const auto replacement = f.engine.play(*plan); expect(replacement.has_value());
        expect(replacement->slot==ref->slot && replacement->generation==ref->generation+1);
        expect(f.engine.frame(*ref,0).error().code==EngineError::stale_handle);
    };
    "a later failing program track cannot publish earlier tracks or other playbacks"_test = [] {
        Fixture f; const auto independent = *f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,8)));
        const auto c = *f.engine.create_object(shape(false,0)), d = *f.engine.create_object(shape(true,80));
        const auto x = *f.engine.create_object(shape(false,1e308)), y = *f.engine.create_object(shape(true,-1e308));
        auto plan = PreparedProgram::prepare(parallel({
            bind(*f.engine.capture_object(c),*f.engine.capture_object(d),{fx::blur(0,4)}),
            bind(*f.engine.capture_object(x),*f.engine.capture_object(y))})); expect(plan.has_value());
        const auto ref = f.engine.play(*plan); expect(ref.has_value());
        const auto ci = plan->tracks()[0].object==c ? 0u : 1u;
        const auto result = f.engine.advance(seconds(.5));
        expect(!result && result.error().code==EngineError::animation && !result.error().causes.empty() && result.error().causes.front().code.starts_with("hanim.program."));
        expect(f.engine.inspect(independent)->position==seconds(0) && f.engine.inspect(*ref)->position==seconds(0));
        expect(f.engine.frame(independent)->effect_values[0]==0 && f.engine.frame(*ref,ci)->effect_values[0]==0);
        expect(f.engine.frame(independent)->geometry.parts[1].local.tx==0 && f.engine.frame(*ref,ci)->geometry.parts[1].local.tx==0);
        expect(f.engine.seek(*ref,seconds(1)).has_value() && f.engine.advance(seconds(.5)).has_value());
        expect(f.engine.inspect(independent)->position==seconds(.5));
    };
    "single track sequences can retarget their actual phase while multitrack replacement is explicit"_test = [] {
        Fixture f; const auto c = *f.engine.create_object(shape(true,80)), d = *f.engine.create_object(shape(true,120));
        const auto a = *f.engine.capture_object(f.a), b = *f.engine.capture_object(f.b);
        const auto plan = PreparedProgram::prepare(sequence({bind(a,b,{fx::blur(0,8)}),bind(a,*f.engine.capture_object(c),{fx::blur(8,12)})}));
        expect(plan.has_value()); const auto ref = *f.engine.play(*plan);
        expect(f.engine.seek(ref,seconds(1.5)).has_value() && f.engine.pause(ref).has_value());
        expect(f.engine.retarget(ref, *f.engine.capture_object(d), {.easing=Ease::stepped(2,StepPosition::jump_start)}).has_value());
        expect(f.engine.frame(ref)->geometry.parts[1].local.tx==60 && f.engine.frame(ref)->effect_values[0]==10);
        expect(f.engine.inspect(ref)->segment_count==1 && f.engine.inspect(ref)->plan_revision==2 && f.engine.inspect(ref)->state==PlaybackState::paused);
        expect(f.engine.release(ref).has_value());
        const auto multiple = PreparedProgram::prepare(parallel({bind(a,b),bind(*f.engine.capture_object(c),*f.engine.capture_object(d))}));
        const auto multi = *f.engine.play(*multiple);
        expect(f.engine.retarget(multi, *f.engine.capture_object(d)).error().code==EngineError::unsupported_operation);
        expect(f.engine.inspect(multi)->track_count==2 && f.engine.inspect(multi)->plan_revision==1);
    };
    "program copies own inputs and playback instances keep separate cursors"_test = [] {
        PreparedProgram plan;
        { Fixture f; const auto d = *f.engine.create_object(shape(false,80)), e = *f.engine.create_object(shape(true,120));
          auto prepared = PreparedProgram::prepare(stagger(seconds(.5),{
              bind(*f.engine.capture_object(f.a),*f.engine.capture_object(f.b)),
              bind(*f.engine.capture_object(d),*f.engine.capture_object(e))})); expect(prepared.has_value()); plan=*prepared; }
        auto first = Engine::make(), second = Engine::make();
        const auto one = first->play(plan), two = second->play(plan); expect(one.has_value() && two.has_value()); plan={};
        expect(first->seek(*one,seconds(1)).has_value() && second->seek(*two,seconds(.25)).has_value());
        expect(first->inspect(*one)->position==seconds(1) && second->inspect(*two)->position==seconds(.25));
        expect(first->frame(*two,0).error().code==EngineError::foreign_engine);
        expect(first->cancel(*one,CancelMode::restore).has_value());
        expect(first->frame(*one,0)->geometry.parts[1].local.tx==0 && first->frame(*one,1)->geometry.parts[1].local.tx==80);
    };
    "prepared program effects cannot bypass the engine's configured per-segment limits"_test = [] {
        Fixture f({.effects={.max_effects=1}});
        const auto plan = PreparedProgram::prepare(bind(*f.engine.capture_object(f.a),*f.engine.capture_object(f.b),
            {fx::blur(0,1),fx::tone({}, {})})); expect(plan.has_value());
        const auto refused = f.engine.play(*plan);
        expect(!refused && refused.error().code==EngineError::effect && refused.error().caused_by(EffectError::capacity));
        expect(f.engine.size()==0);
        expect(f.engine.play(anim::morph(f.a, f.b).with_effect(fx::blur(0,1))).has_value());
    };
    "phase changes expose actual geometry and effect ranges rather than maximum scratch capacity"_test = [] {
        Fixture f;
        const auto before=*f.engine.capture_object(f.a), target=*f.engine.capture_object(f.b);
        expect(f.engine.update_object(f.a,{{{.id="group"}}}).has_value());
        const auto newer=*f.engine.capture_object(f.a);
        const auto other=*f.engine.create_object({{{.id="group",.local={.tx=20}}}});
        const auto plan=PreparedProgram::prepare(sequence({bind(before,target,{fx::blur(0,8)}),
            bind(newer,*f.engine.capture_object(other),{}, {},{},ProgramSource::snapshot)}));
        expect(plan.has_value());const auto ref=*f.engine.play(*plan);
        expect(f.engine.seek(ref,seconds(.5)).has_value());
        auto frame=f.engine.frame(ref);
        expect(frame->geometry.parts.size()==2 && !frame->geometry.cubics.empty() && frame->effect_values.size()==1);
        expect(frame->geometry.source_revision==1);
        expect(f.engine.seek(ref,seconds(1.5)).has_value());frame=f.engine.frame(ref);
        expect(frame->geometry.parts.size()==1 && frame->geometry.cubics.empty() && frame->effect_values.empty());
        expect(frame->geometry.parts[0].local.tx==10 && frame->geometry.source_revision==2);
        expect(f.engine.seek(ref,seconds(.5)).has_value());frame=f.engine.frame(ref);
        expect(frame->geometry.parts.size()==2 && !frame->geometry.cubics.empty() && frame->effect_values.size()==1);
        expect(frame->geometry.parts[1].local.tx==20 && frame->effect_values[0]==4);
    };
    "route storage budgets remain transactional across play retarget and release"_test = [] {
        auto made=Engine::make({.max_total_route_segments=2});expect(made.has_value());if(!made)return;
        auto& engine=*made;const auto a=*engine.create_object(shape(false,0)),b=*engine.create_object(shape(false,0));
        MotionPath path;path.line_to({10,0}).line_to({10,10});
        const auto first=engine.play(anim::follow_path(a,path));expect(first.has_value());if(!first)return;
        expect(engine.seek_progress(*first,.5).has_value());const auto position=engine.frame(*first)->geometry.parts[0].local;
        const auto denied=engine.play(anim::follow_path(b,path));expect(!denied && denied.error().code==EngineError::capacity);
        expect(engine.frame(*first)->geometry.parts[0].local==position && engine.plan(*first)->route_segment_count()==2);
        expect(engine.retarget(*first,PropertyTargets{.position=Vec2{20,0}}).has_value());
        expect(engine.frame(*first)->geometry.parts[0].local==position && engine.plan(*first)->route_segment_count()==0);
        const auto second=engine.play(anim::follow_path(b,path));expect(second.has_value());if(!second)return;
        expect(engine.release(*second).has_value());
        const auto replacement=engine.play(anim::follow_path(b,path));expect(replacement.has_value());
        if(replacement)expect(engine.release(*replacement).has_value());expect(engine.release(*first).has_value());
    };
}};
