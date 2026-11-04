// External authoring consumer: real public geometry, plans and player controls.
import std;
import hanim.validation.api_probe;
using namespace std::chrono_literals;
namespace h = hanim;
namespace api = api_probe;
namespace anim = api_probe::anim;
namespace {
struct Check { std::string_view name; bool ok; };
std::vector<Check> checks;
bool check(bool value, std::string_view name) { checks.push_back({name,value}); return value; }
int finish() {
    const bool ok = std::ranges::all_of(checks, &Check::ok);
    std::print("{{\"kind\":\"hanim.api-probe\",\"ok\":{},\"checks\":[",ok);
    for(std::size_t i=0;i<checks.size();++i)
        std::print("{}{{\"name\":\"{}\",\"ok\":{}}}",i?",":"",checks[i].name,checks[i].ok);
    std::println("]}}");
    return ok?0:1;
}
std::expected<h::ObjectDescription,h::ObjectError> shape(bool circle,double x) {
    constexpr double k=.5522847498307936;
    const std::vector<h::ShapeSegment> edges = circle ? std::vector<h::ShapeSegment>{
        h::ShapeSegment::cubic({k,-1},{1,-k},{1,0}),h::ShapeSegment::cubic({1,k},{k,1},{0,1}),
        h::ShapeSegment::cubic({-k,1},{-1,k},{-1,0}),h::ShapeSegment::cubic({-1,-k},{-k,-1},{0,-1})} :
        std::vector<h::ShapeSegment>{h::ShapeSegment::line({-1,1}),h::ShapeSegment::line({1,1})};
    auto contour=h::Contour::copy({{0,-1},edges,true});
    if(!contour)return std::unexpected(contour.error());
    return h::ObjectDescription{{{.id="shape",.local={.tx=x},.contours={*contour},.fill=h::Rgba{1,0,0,1}}}};
}
struct Pose {
    std::vector<h::Transform2D> transforms;
    std::vector<h::ShapeCubic> cubics;
    std::vector<float> effects;
};
Pose copy(const h::EngineFrame& frame) {
    Pose result;
    for(const auto& part:frame.geometry.parts)result.transforms.push_back(part.local);
    result.cubics.assign(frame.geometry.cubics.begin(),frame.geometry.cubics.end());
    result.effects.assign(frame.effect_values.begin(),frame.effect_values.end());
    return result;
}
std::array<double,8> coordinates(const h::ShapeCubic& c) {
    return {c.start.x,c.start.y,c.control1.x,c.control1.y,c.control2.x,c.control2.y,c.end.x,c.end.y};
}
bool equal(const Pose& a,const Pose& b) {
    if(a.transforms!=b.transforms || a.effects!=b.effects || a.cubics.size()!=b.cubics.size())return false;
    for(std::size_t i=0;i<a.cubics.size();++i)if(coordinates(a.cubics[i])!=coordinates(b.cubics[i]))return false;
    return true;
}
static_assert(api::Animation<api::Morph> && api::Animation<api::Description>);
static_assert(!api::Animation<int> && !api::Animation<h::ObjectRef>);
template<class T> concept HasGroupEffect = requires(const T& value) { value.with_effect(h::fx::blur(0,1)); };
static_assert(HasGroupEffect<api::Morph> && !HasGroupEffect<api::Description>);
}
int main() {
    auto host=h::ObjectRegistry::make(); auto made=h::Engine::make();
    if(!check(bool(host)&&bool(made),"registry_and_engine_created"))return finish();
    auto& engine=*made; api::HEngine hengine{engine};
    const auto da=shape(false,0),db=shape(true,40),dc=shape(false,100);
    if(!check(bool(da)&&bool(db)&&bool(dc),"public_contours_created"))return finish();
    const auto ar=host->create(*da),br=host->create(*db),cr=host->create(*dc);
    if(!check(bool(ar)&&bool(br)&&bool(cr),"object_refs_created"))return finish();
    const auto a=host->capture(*ar),b=host->capture(*br),c=host->capture(*cr);
    if(!check(bool(a)&&bool(b)&&bool(c),"owned_snapshots_created"))return finish();

    const auto base=anim::morph(*a,*b);
    const auto quick=base.duration(600ms).easing(h::ease::linear).with_effect(h::fx::blur(0,8));
    const auto slow=base.duration(1200ms).with_effect(h::fx::tone({},{}));
    check(engine.size()==0 && host->revision(*ar)==1 && a->parts()[0].local.tx==0,
          "description_construction_has_no_playback_or_object_side_effect");
    check(base.binding().options.duration==h::seconds(1) && base.binding().effects.empty() &&
          quick.binding().options.duration==h::seconds(.6) && quick.binding().effects[0].id=="blur" &&
          slow.binding().options.duration==h::seconds(1.2) && slow.binding().effects[0].id=="tone",
          "fluent_values_reuse_without_mutating_recipe");
    auto prepared=hengine.prepare(quick);
    if(!check(bool(prepared),"explicit_prepare_succeeds"))return finish();
    check(prepared->duration()==h::seconds(.6) && engine.size()==0,"prepare_does_not_start_clock");
    std::array<Pose,5> reference;
    constexpr std::array times{0.,.15,.3,.45,.6};
    constexpr std::array pose_names{"convenient_pose_t0", "convenient_pose_t015", "convenient_pose_t030",
                                    "convenient_pose_t045", "convenient_pose_t060"};
    auto playback=hengine.play(quick);
    if(!check(bool(playback),"convenient_play_uses_public_engine"))return finish();
    for(std::size_t i=0;i<times.size();++i) {
        const auto seek=engine.seek(*playback,h::seconds(times[i])); const auto frame=engine.frame(*playback);
        if(!check(bool(seek)&&bool(frame),pose_names[i]))return finish();
        reference[i]=copy(*frame);
    }
    bool midpoint=reference[0].cubics.size()==reference[2].cubics.size() && reference[2].cubics.size()==reference[4].cubics.size();
    bool changed=false;
    for(std::size_t i=0;midpoint && i<reference[2].cubics.size();++i) {
        const auto start=coordinates(reference[0].cubics[i]),mid=coordinates(reference[2].cubics[i]),end=coordinates(reference[4].cubics[i]);
        for(std::size_t j=0;j<start.size();++j) { midpoint &= std::abs(mid[j]-(start[j]+end[j])*.5)<1e-12; changed |= start[j]!=end[j]; }
    }
    check(midpoint && changed && reference[2].transforms[0].tx==20 && reference[2].effects==std::vector<float>{4},
          "true_cubic_geometry_transform_effect_midpoint");
    check(host->revision(*ar)==1 && host->capture(*ar)->parts()[0].local.tx==0,"playback_never_writes_business_object");
    if(!check(bool(engine.release(*playback)),"release_convenient_instance"))return finish();
    auto replay=hengine.play(*prepared);
    if(!check(bool(replay),"prepared_play_succeeds"))return finish();
    bool same=true;
    for(const auto i:std::array<std::size_t,5>{4,1,3,0,2}) {
        const auto seek=engine.seek(*replay,h::seconds(times[i]));const auto frame=engine.frame(*replay);
        if(!seek||!frame) {same=false;break;} same &= equal(reference[i],copy(*frame));
    }
    check(same,"convenient_and_explicit_prepare_identical_under_unordered_seek");
    const auto conflict=hengine.play(quick);
    check(!conflict && conflict.error().code==h::EngineError::binding_conflict,"occupied_source_error_is_preserved");
    if(!check(bool(engine.release(*replay)),"release_prepared_instance"))return finish();
    check(!engine.frame(*replay),"released_handle_is_stale");

    const auto layered=quick.with_effect(h::fx::tone({.r=0,.g=0,.b=0,.amount=0},{.r=1,.g=1,.b=1,.amount=1}));
    const auto layered_plan=hengine.prepare(layered);
    if(!check(bool(layered_plan),"ordered_effect_chain_prepares"))return finish();
    auto layered_play=hengine.play(*layered_plan);
    if(!check(bool(layered_play)&&bool(engine.seek(*layered_play,h::seconds(.3))),"ordered_effect_chain_samples"))return finish();
    const auto layered_frame=engine.frame(*layered_play);
    check(layered_frame && layered_frame->effects->effects()[0].id=="blur" && layered_frame->effects->effects()[1].id=="tone" &&
          copy(*layered_frame).effects==std::vector<float>{4,.5f,.5f,.5f,.5f} && quick.binding().effects.size()==1,
          "leaf_effects_preserve_order_values_and_fluent_reuse");
    if(!check(bool(engine.release(*layered_play)),"release_layered_effects"))return finish();

    const auto series=api::sequence(quick,anim::morph(*a,*c).duration(600ms));
    const auto series_plan=hengine.prepare(series);
    if(!check(bool(series_plan),"sequence_prepares_on_existing_compiler"))return finish();
    auto series_play=hengine.play(*series_plan);
    if(!check(bool(series_play),"sequence_plays"))return finish();
    if(!check(bool(engine.seek(*series_play,h::seconds(.6))),"sequence_boundary_seek"))return finish();
    const auto boundary=copy(*engine.frame(*series_play));
    if(!check(bool(engine.seek(*series_play,h::seconds(.9))),"sequence_second_segment_seek"))return finish();
    check(boundary.transforms[0].tx==40 && std::abs(engine.frame(*series_play)->geometry.parts[0].local.tx-70)<1e-10,
          "sequence_second_source_is_preceding_endpoint");
    if(!check(bool(engine.release(*series_play)),"release_sequence"))return finish();

    const auto combined=api::parallel(series,anim::morph(*b,*c).duration(1200ms));
    auto both=hengine.play(combined);
    if(!check(bool(both),"nested_parallel_plays"))return finish();
    const auto info=engine.inspect(*both);
    check(info && info->track_count==2 && info->segment_count==3,"parallel_retains_two_tracks_three_segments");
    const auto ambiguous=engine.frame(*both);
    check(!ambiguous && ambiguous.error().code==h::EngineError::ambiguous_frame && bool(engine.frame(*both,1)),
          "multitrack_requires_explicit_frame_selection");
    if(!check(bool(engine.release(*both)),"release_parallel"))return finish();
    const auto delayed=api::stagger(200ms,quick,anim::morph(*b,*c).duration(600ms));
    const auto delayed_plan=hengine.prepare(delayed);
    if(!check(bool(delayed_plan),"stagger_prepares"))return finish();
    auto delayed_play=hengine.play(*delayed_plan);
    if(!check(bool(delayed_play)&&bool(engine.seek(*delayed_play,h::seconds(.1))),"stagger_before_second_start"))return finish();
    check(delayed_plan->duration()==h::seconds(.8) && engine.frame(*delayed_play,1)->geometry.parts[0].local.tx==40,
          "stagger_holds_unstarted_track_source");
    if(!check(bool(engine.release(*delayed_play)),"release_stagger"))return finish();

    const auto invalid=hengine.play(base.duration(-1ms));
    check(!invalid && invalid.error().code==h::EngineError::program && invalid.error().program &&
          invalid.error().program->code==h::ProgramError::invalid_time && engine.size()==0,
          "invalid_duration_keeps_structured_program_error_and_no_playback");
    const auto empty=hengine.prepare(anim::morph(h::Snapshot{},*b));
    check(!empty && empty.error().program && empty.error().program->code==h::ProgramError::invalid_snapshot,"invalid_snapshot_is_rejected");
    const auto overlap=hengine.prepare(api::parallel(quick,quick));
    check(!overlap && overlap.error().program && overlap.error().program->code==h::ProgramError::binding_conflict,"same_object_overlap_is_not_silently_blended");
    h::ProgramLimits limits;limits.max_nodes=1;
    const auto limited=hengine.prepare(series,limits);
    check(!limited && limited.error().program && limited.error().program->code==h::ProgramError::node_budget,"prepare_respects_node_budget");
    api::HEngine restricted{engine,limits};
    const auto implicit_limited=restricted.play(series);
    check(!implicit_limited && implicit_limited.error().program && implicit_limited.error().program->code==h::ProgramError::node_budget,
          "convenient_prepare_respects_configured_limits");
    const auto default_limited=restricted.prepare(series);
    check(!default_limited && default_limited.error().program && default_limited.error().program->code==h::ProgramError::node_budget,
          "explicit_prepare_uses_same_default_limits_as_convenient_play");
    auto badfx=h::fx::blur(0,1);badfx.parameters.push_back(badfx.parameters.front());
    const auto effect_error=hengine.prepare(base.with_effect(std::move(badfx)));
    check(!effect_error && effect_error.error().program && effect_error.error().program->code==h::ProgramError::effect &&
          effect_error.error().program->effect==h::EffectError::duplicate_parameter,
          "effect_validation_error_is_preserved");
    const auto updated=host->update(*ar,*dc);
    check(updated && *updated==2 && a->revision()==1 && a->parts()[0].local.tx==0,"snapshot_stays_frozen_after_host_update");
    check(bool(host->destroy(*ar)) && bool(host->destroy(*br)) && bool(host->destroy(*cr)),"host_objects_destroyed");
    auto frozen=hengine.play(*prepared);
    if(!check(bool(frozen)&&bool(engine.seek(*frozen,h::seconds(.3))),"prepared_plan_survives_destroyed_host_objects"))return finish();
    check(equal(reference[2],copy(*engine.frame(*frozen))),"frozen_geometry_retains_owned_inputs");
    if(!check(bool(engine.release(*frozen)),"release_frozen_snapshot_playback"))return finish();

    // The default authoring path takes identities. Capture occurs only at
    // prepare/play; a prepared program owns the resolved revision thereafter.
    const auto live_a=engine.create_object(*da),live_b=engine.create_object(*db),live_c=engine.create_object(*dc);
    if(!check(bool(live_a)&&bool(live_b)&&bool(live_c),"live_engine_objects_created"))return finish();
    const auto live_base=anim::morph(*live_a,*live_b);
    const auto live_quick=live_base.duration(600ms).with_effect(h::fx::blur(0,8));
    check(engine.size()==0 && engine.capture_object(*live_a)->revision()==1,
          "object_ref_description_has_no_capture_or_play_side_effect");
    auto shifted=*da;shifted.parts[0].local.tx=10;
    if(!check(bool(engine.update_object(*live_a,shifted)),"live_source_updated_after_description"))return finish();
    auto live_plan=hengine.prepare(live_quick);
    if(!check(bool(live_plan),"object_ref_prepare_captures_current_revision"))return finish();
    auto live_play=hengine.play(live_quick);
    if(!check(bool(live_play)&&bool(engine.seek(*live_play,h::seconds(.3))),"object_ref_convenient_play"))return finish();
    const auto live_mid=copy(*engine.frame(*live_play));
    check(live_plan->tracks()[0].source_revision==2 && live_mid.transforms[0].tx==25,
          "capture_is_deferred_until_prepare_not_description_creation");
    if(!check(bool(engine.release(*live_play)),"release_live_convenient"))return finish();
    auto live_replay=hengine.play(*live_plan);
    if(!check(bool(live_replay)&&bool(engine.seek(*live_replay,h::seconds(.3))),"object_ref_prepared_play"))return finish();
    check(equal(live_mid,copy(*engine.frame(*live_replay))),"object_ref_convenient_and_prepared_equivalent");
    if(!check(bool(engine.release(*live_replay)),"release_live_prepared"))return finish();

    const auto live_series=api::sequence(live_quick,anim::morph(*live_a,*live_c).duration(600ms));
    auto live_series_play=hengine.play(live_series);
    if(!check(bool(live_series_play)&&bool(engine.seek(*live_series_play,h::seconds(.9))),"object_ref_sequence_plays"))return finish();
    check(std::abs(engine.frame(*live_series_play)->geometry.parts[0].local.tx-70)<1e-10,
          "object_ref_sequence_uses_previous_endpoint");
    if(!check(bool(engine.release(*live_series_play)),"release_live_sequence"))return finish();
    auto depth_limits=h::ProgramLimits{};depth_limits.max_depth=1;
    const auto deep=hengine.prepare(api::parallel(live_quick),depth_limits);
    check(!deep && deep.error().program && deep.error().program->code==h::ProgramError::depth_budget,
          "authoring_lowering_depth_is_bounded");
    const auto foreign=hengine.prepare(anim::morph(*ar,*live_b));
    check(!foreign && foreign.error().code==h::EngineError::object && foreign.error().object==h::ObjectError::foreign_owner,
          "foreign_owner_error_is_preserved");
    check(bool(engine.destroy_object(*live_a)) && bool(engine.destroy_object(*live_b)) && bool(engine.destroy_object(*live_c)),
          "live_objects_destroyed");
    const auto stale=hengine.play(live_quick);
    check(!stale && stale.error().code==h::EngineError::object && stale.error().object==h::ObjectError::stale_object && engine.size()==0,
          "stale_description_rejected_without_partial_playback");
    const std::array reused{engine.create_object(*da),engine.create_object(*db),engine.create_object(*dc)};
    if(!check(std::ranges::all_of(reused,[](const auto& ref){return bool(ref);}),"new_generation_objects_created"))return finish();
    check(std::ranges::any_of(reused,[&](const auto& ref){
              return ref->owner==live_a->owner && ref->slot==live_a->slot && ref->generation!=live_a->generation;
          }),"destroyed_source_slot_was_reused_with_new_generation");
    const auto reused_stale=hengine.prepare(live_quick);
    check(!reused_stale && reused_stale.error().object==h::ObjectError::stale_object,
          "old_ref_does_not_rebind_after_slot_reuse");
    bool cleaned=true;
    for(const auto& ref:reused)cleaned &= bool(engine.destroy_object(*ref));
    check(cleaned,"new_generation_objects_destroyed");
    const auto live_frozen=hengine.play(*live_plan);
    if(!check(bool(live_frozen)&&bool(engine.seek(*live_frozen,h::seconds(.3))),"prepared_ref_plan_survives_object_destruction"))return finish();
    check(equal(live_mid,copy(*engine.frame(*live_frozen))),"prepared_ref_plan_keeps_owned_geometry");
    check(bool(engine.release(*live_frozen)) && engine.size()==0,"all_playback_resources_released");
    return finish();
}
