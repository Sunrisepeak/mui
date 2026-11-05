// External authoring consumer: real public geometry, plans and player controls.
import std;
import hanim;
using namespace std::chrono_literals;
namespace h = hanim;
namespace api = hanim;
namespace anim = hanim::anim;
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
static_assert(api::Animation<anim::Morph> && api::Animation<h::Program> && api::Animation<anim::Composite>);
static_assert(!api::Animation<int> && !api::Animation<h::ObjectRef>);
template<class T> concept HasGroupEffect = requires(const T& value) { value.with_effect(h::fx::blur(0,1)); };
static_assert(HasGroupEffect<anim::Morph> && HasGroupEffect<anim::Composite> && !HasGroupEffect<h::Program>);
}
int main() {
    auto host=h::ObjectRegistry::make(); auto made=h::Engine::make();
    if(!check(bool(host)&&bool(made),"registry_and_engine_created"))return finish();
    auto& engine=*made; auto& hengine=engine;
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

    const auto curved=quick.with_effect(h::fx::vertex_path(h::PointPath::bend(.35)))
                           .with_effect(h::fx::translation_path(h::PointPath::bend(.25)));
    const auto curved_plan=hengine.prepare(curved);
    if(!check(bool(curved_plan),"typed_process_paths_prepare"))return finish();
    auto curved_play=hengine.play(curved);
    if(!check(bool(curved_play)&&bool(engine.seek(*curved_play,h::seconds(.3))),"typed_process_paths_play"))return finish();
    const auto curved_frame=engine.frame(*curved_play);
    if(!check(bool(curved_frame),"typed_process_paths_expose_frame"))return finish();
    const auto curved_pose=copy(*curved_frame);
    check(curved_pose.transforms[0].tx==20 && curved_pose.transforms[0].ty==10 && curved_pose.effects==reference[2].effects,
          "translation_path_is_typed_and_image_effect_is_retained");
    check(!std::ranges::equal(curved_pose.cubics,reference[2].cubics,[](const auto& x,const auto& y){return coordinates(x)==coordinates(y);}),
          "vertex_path_changes_geometry_before_renderer");
    bool endpoints=true;
    for(auto i:std::array<std::size_t,2>{0,4}) {
        const auto seek=engine.seek(*curved_play,h::seconds(times[i]));const auto frame=engine.frame(*curved_play);
        endpoints=endpoints && bool(seek) && bool(frame) && equal(copy(*frame),reference[i]);
    }
    check(endpoints,"process_paths_preserve_exact_endpoint_channels");
    if(!check(bool(engine.release(*curved_play)),"release_process_path_instance"))return finish();
    const auto curved_replay=hengine.play(*curved_plan);
    if(!check(bool(curved_replay)&&bool(engine.seek(*curved_replay,h::seconds(.3))),"prepared_process_path_replay"))return finish();
    const auto replayed_path=engine.frame(*curved_replay);
    check(replayed_path && equal(copy(*replayed_path),curved_pose),"process_path_convenient_and_prepared_equal");
    if(!check(bool(engine.release(*curved_replay)),"release_process_path_replay"))return finish();

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
    check(!invalid && invalid.error().code==h::EngineError::invalid_time && invalid.error().program &&
          invalid.error().program->code==h::ProgramError::invalid_time && engine.size()==0,
          "invalid_duration_keeps_structured_program_error_and_no_playback");
    const auto authored=h::invalid_program({.code=h::ProgramError::invalid_time,.node=99});
    const auto authored_play=hengine.play(api::sequence(quick,authored));
    check(authored.kind()==h::ProgramKind::invalid && authored.failure()->node==99 && !authored_play &&
          authored_play.error().program && authored_play.error().program->code==h::ProgramError::invalid_time &&
          authored_play.error().program->node==2 && engine.size()==0,
          "deferred_authoring_error_keeps_its_cause_with_actual_node_and_no_playback");
    const auto bad_feedback=anim::pulse(*a,{0,1});
    const auto refused_feedback=hengine.prepare(bad_feedback);
    check(bad_feedback.failure() && !refused_feedback && refused_feedback.error().program &&
          refused_feedback.error().program->animation &&
          refused_feedback.error().program->animation->code==h::MorphError::invalid_property,
          "feedback_invalid_arithmetic_is_a_typed_authoring_failure");
    const auto empty=hengine.prepare(anim::morph(h::Snapshot{},*b));
    check(!empty && empty.error().program && empty.error().program->code==h::ProgramError::invalid_snapshot,"invalid_snapshot_is_rejected");
    const auto overlap=hengine.prepare(api::parallel(quick,quick));
    check(!overlap && overlap.error().program && overlap.error().program->code==h::ProgramError::binding_conflict,"same_object_overlap_is_not_silently_blended");
    h::ProgramLimits limits;limits.max_nodes=1;
    const auto limited=hengine.prepare(series,limits);
    check(!limited && limited.error().program && limited.error().program->code==h::ProgramError::node_budget,"prepare_respects_node_budget");
    const auto implicit_limited=hengine.play(series,limits);
    check(!implicit_limited && implicit_limited.error().program && implicit_limited.error().program->code==h::ProgramError::node_budget,
          "convenient_prepare_respects_configured_limits");
    const auto default_limited=hengine.prepare(series,limits);
    check(!default_limited && default_limited.error().program && default_limited.error().program->code==h::ProgramError::node_budget,
          "explicit_prepare_uses_same_limits_as_convenient_play");
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
    const auto unresolved=h::PreparedProgram::prepare(live_quick);
    check(!unresolved && unresolved.error().code==h::ProgramError::unresolved_object && unresolved.error().node==0,
          "standalone_compiler_requires_registry_for_live_inputs");
    const auto independent=h::PreparedProgram::prepare(anim::morph(*a,*b));
    check(bool(independent),"snapshot_recipe_prepares_without_engine_or_registry");
    const auto registry_recipe=h::PreparedProgram::prepare(anim::morph(*live_a,*live_b),*host);
    check(!registry_recipe && registry_recipe.error().code==h::ProgramError::object &&
          registry_recipe.error().object==h::ObjectError::foreign_owner,
          "explicit_registry_rejects_foreign_live_ref");
    const auto mixed=hengine.prepare(anim::morph(*live_a,*b));
    check(mixed && mixed->segments()[0].animation.from().revision()==2 &&
          mixed->segments()[0].animation.to().object()==b->object(),"live_source_accepts_frozen_external_target");
    const auto advanced=hengine.prepare(api::sequence(h::bind(*a,*b),anim::morph(*a,*c).duration(600ms)));
    check(advanced && advanced->segments().size()==2 && advanced->duration()==h::seconds(1.6),
          "legacy_snapshot_program_composes_with_public_recipe");
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
    const auto reset=hengine.play(api::sequence(live_quick,anim::morph(*live_a,*live_c)
        .duration(600ms).source(h::ProgramSource::snapshot)));
    if(!check(reset && engine.seek(*reset,h::seconds(.6)),"explicit_reset_sequence_plays"))return finish();
    check(engine.frame(*reset)->geometry.parts[0].local.tx==10,"explicit_source_mode_resets_to_captured_pose");
    if(!check(bool(engine.release(*reset)),"release_reset_sequence"))return finish();
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
    const auto scoped_recipe=h::composite(h::parallel(anim::morph(*a,*b),
        h::composite(anim::morph(*c,*b)).with_effect(h::fx::blur(0,4)))).with_effect(h::fx::blur(0,8));
    const auto scoped=hengine.prepare(scoped_recipe);
    if(!check(bool(scoped),"public_composite_prepares_without_renderer"))return finish();
    check(scoped->nodes().size()==5 && scoped->scopes().size()==2 && scoped->scopes()[1].parent==0,
          "scope_hierarchy_retained_by_the_public_compiler");
    const auto group=hengine.play(*scoped);
    if(!check(group && hengine.seek_progress(*group,.5),"public_composite_plays_on_the_shared_clock"))return finish();
    const auto scene=hengine.program_frame(*group);
    check(scene && scene->scene.tracks.size()==2 && scene->scene.scope_effect_values.size()==2 &&
          scene->scene.scope_effect_values[0]==4 && scene->scene.scope_effect_values[1]==2 &&
          scene->scene.preparation==scoped->identity(),"program_frame_publishes_geometry_and_scope_values_together");
    check(bool(hengine.cancel(*group,h::CancelMode::restore)) &&
          hengine.program_frame(*group)->scene.scope_effect_values[0]==0,"composite_restore_uses_exact_source_parameters");
    check(bool(hengine.release(*group)) && hengine.size()==0,"composite_releases_all_playback_resources");
    const auto blocks=hengine.play(anim::morph(*a,*b).easing(h::ease::linear)
        .with_effect(h::fx::pixelate({4,0},{12,1},{h::fx::PixelUnit::device,-2,3})));
    if(!check(bool(blocks),"public_pixelate_recipe_plays_without_renderer"))return finish();
    check(bool(hengine.seek_progress(*blocks,.5)),"pixelate_parameters_seek_on_engine_clock");
    const auto block_frame=hengine.frame(*blocks);
    check(block_frame && std::ranges::equal(block_frame->effect_values,std::array<float,5>{8,.5f,1,-2,3}),
          "pixelate_samples_strength_size_and_fixed_grid");
    check(bool(hengine.release(*blocks)),"pixelate_resources_released");
    const auto property_object=hengine.create_object(*shape(false,3));
    if(!check(bool(property_object),"property_object_created_via_public_engine"))return finish();
    const auto property_recipe=h::sequence(anim::move_to(*property_object,{15,6}),
        anim::move_by(*property_object,{8,4}),anim::rotate_to(*property_object,std::numbers::pi),
        anim::scale_to(*property_object,{2,3}),anim::fill_to(*property_object,{0,0,1,1}));
    const auto property_plan=hengine.prepare(property_recipe);
    if(!check(property_plan && property_plan->duration()==5s && property_plan->segments().size()==5,
        "five_property_recipes_share_public_compiler"))return finish();
    const auto property_play=hengine.play(*property_plan);
    if(!check(bool(property_play),"prepared_property_program_plays"))return finish();
    bool chained=true;
    for(int i=0;i<=300;++i) {
        const double t=i/60.;chained=chained && bool(hengine.seek(*property_play,h::seconds(t)));
        const auto sample=hengine.frame(*property_play);
        if(!sample){chained=false;break;}
        const auto transform=sample->geometry.parts[0].local;
        if(t>=2)chained=chained && transform.tx==23 && transform.ty==10;
        if(t>=3)chained=chained && transform.rotate==std::numbers::pi;
        if(t>=4)chained=chained && transform.sx==2 && transform.sy==3;
    }
    check(chained,"property_sequence_keeps_unmodified_lanes_through_301_samples");
    check(hengine.capture_object(*property_object)->parts()[0].local.tx==3,
        "property_program_never_mutates_registry_or_creates_target_objects");
    check(bool(hengine.seek(*property_play,1500ms)),"property_retarget_selects_current_pose");
    const auto held=copy(*hengine.frame(*property_play));
    check(bool(hengine.pause(*property_play)) && bool(hengine.retarget(*property_play,h::PropertyTargets{.offset=h::Vec2{9,-3}})),
        "property_retarget_from_current_frame_accepted");
    check(equal(held,copy(*hengine.frame(*property_play))),"property_retarget_preserves_complete_current_pose");
    check(bool(hengine.seek(*property_play,1s)),"property_retarget_reaches_endpoint");
    const auto property_end=hengine.frame(*property_play);
    check(property_end && property_end->geometry.parts[0].local.tx==held.transforms[0].tx+9 &&
        property_end->geometry.parts[0].local.ty==held.transforms[0].ty-3,"relative_retarget_uses_current_pose_not_original_snapshot");
    const auto old_revision=hengine.inspect(*property_play)->plan_revision;
    const auto invalid_property=hengine.retarget(*property_play,h::PropertyTargets{.rotation=std::numeric_limits<double>::quiet_NaN()});
    check(!invalid_property && hengine.inspect(*property_play)->plan_revision==old_revision,
        "invalid_property_retarget_preserves_playback");
    check(bool(hengine.release(*property_play)),"property_resources_released");
    const auto relative=hengine.play(h::sequence(
        anim::to(*property_object,{.rotation=.5,.scale=h::Vec2{2,3}}),
        h::parallel(anim::rotate_by(*property_object,.5),anim::scale_by(*property_object,{2,.5}))));
    if(!check(relative && hengine.seek(*relative,1500ms),"public_relative_rotation_and_scale_compose_after_nonidentity_sources"))return finish();
    const auto relative_pose=hengine.frame(*relative)->geometry.parts[0].local;
    check(relative_pose.rotate==.75 && relative_pose.sx==3 && relative_pose.sy==2.25,
        "relative_recipes_use_prior_channel_values_not_registry_defaults");
    const auto relative_held=copy(*hengine.frame(*relative));
    check(bool(hengine.retarget(*relative,h::PropertyTargets{.rotation_offset=.5,.scale_factor=h::Vec2{.5,2}})) &&
        equal(relative_held,copy(*hengine.frame(*relative))),"relative_retarget_starts_at_the_complete_current_pose");
    check(bool(hengine.seek(*relative,1s)),"relative_retarget_samples_its_endpoint");
    const auto relative_end=hengine.frame(*relative)->geometry.parts[0].local;
    check(relative_end.rotate==1.25 && relative_end.sx==1.5 && relative_end.sy==4.5,
        "relative_retarget_adds_rotation_and_multiplies_scale");
    check(bool(hengine.release(*relative)),"relative_playback_resources_released");
    const auto feedback=hengine.play(h::parallel(
        anim::pulse(*property_object,{2,2},{.duration=1s}),
        anim::shake(*property_object,{8,0},{.duration=1s}),
        anim::wobble(*property_object,.2,{.duration=1s})));
    if(!check(bool(feedback),"public_feedback_recipes_are_ordinary_programs"))return finish();
    const auto feedback_source=copy(*hengine.frame(*feedback));
    check(bool(hengine.seek(*feedback,500ms)),"feedback_samples_without_an_event_loop");
    const auto feedback_peak=hengine.frame(*feedback)->geometry.parts[0].local;
    check(feedback_peak.sx==2 && std::abs(feedback_peak.tx-3)<1e-12 && std::abs(feedback_peak.rotate)<1e-12,
        "independent_feedback_channels_have_their_defined_midpoints");
    check(bool(hengine.seek(*feedback,1s)) && equal(feedback_source,copy(*hengine.frame(*feedback))),
        "feedback_returns_to_its_source_without_registry_mutation");
    check(bool(hengine.release(*feedback)),"feedback_resources_released");
    const auto image_scope=hengine.play(h::composite(anim::to(*property_object,
        {.position=h::Vec2{20,5},.rotation=std::numbers::pi,.scale=h::Vec2{2,2}})
        .with_effect(h::fx::translation_path(h::PointPath::bend(.25)))).with_effect(h::fx::blur(0,4)));
    if(!check(image_scope && hengine.seek(*image_scope,500ms),"typed_property_target_composes_process_and_image_scope"))return finish();
    check(hengine.program_frame(*image_scope)->scene.scope_effect_values[0]==2,
        "property_scope_parameters_use_shared_playback_time");
    check(bool(hengine.release(*image_scope)) && bool(hengine.destroy_object(*property_object)),
        "public_property_consumer_releases_all_resources");
    auto group_description=*shape(false,0);
    group_description.parts[0].parent="group";
    auto title=group_description.parts[0];title.id="title";title.local.tx=8;
    group_description.parts.push_back(title);
    auto fixed=title;fixed.id="badge";fixed.local.tx=16;group_description.parts.push_back(fixed);
    group_description.parts.push_back({.id="group"});
    const auto selected_object=hengine.create_object(group_description);
    if(!check(bool(selected_object),"selected_parts_object_created"))return finish();
    const auto selected=hengine.play(anim::to(*selected_object,h::PropertySet{
        {.position=h::Vec2{20,4},.part="shape"},
        {.rotation=std::numbers::pi,.part="title"}, {.scale=h::Vec2{2,2},.part="title"}}));
    if(!check(selected && hengine.seek(*selected,500ms),"multi_part_property_set_plays_through_public_api"))return finish();
    const auto selected_pose=copy(*hengine.frame(*selected));
    bool selected_values=true;
    for(const auto& part:hengine.frame(*selected)->geometry.parts) {
        if(part.info.id=="shape")selected_values &= part.local.tx==10 && part.local.ty==2;
        if(part.info.id=="title")selected_values &= part.local.tx==8 && part.local.rotate==std::numbers::pi/2 && part.local.sx==1.5;
        if(part.info.id=="badge")selected_values &= part.local.tx==16 && part.local.rotate==0 && part.local.sx==1;
    }
    check(selected_values,"selected_properties_and_unselected_sibling_remain_independent");
    check(bool(hengine.retarget(*selected,h::PropertySet{
        {.offset=h::Vec2{-4,2},.part="shape"}, {.rotation=0,.part="title"}})),
        "multi_part_retarget_accepts_one_atomic_target_set");
    check(equal(selected_pose,copy(*hengine.frame(*selected))),"multi_part_retarget_starts_at_complete_current_pose");
    check(bool(hengine.seek(*selected,1s)),"multi_part_retarget_reaches_end");
    bool final_values=true;
    for(const auto& part:hengine.frame(*selected)->geometry.parts) {
        if(part.info.id=="shape")final_values &= part.local.tx==6 && part.local.ty==4;
        if(part.info.id=="title")final_values &= part.local.rotate==0 && part.local.sx==1.5;
        if(part.info.id=="badge")final_values &= part.local.tx==16;
    }
    check(final_values,"retarget_preserves_unwritten_part_channels");
    const auto selected_revision=hengine.inspect(*selected)->plan_revision;
    const auto duplicate=hengine.retarget(*selected,h::PropertySet{
        {.position=h::Vec2{1,2},.part="title"}, {.offset=h::Vec2{3,4},.part="title"}});
    check(!duplicate && hengine.inspect(*selected)->plan_revision==selected_revision,
        "overlapping_property_set_is_rejected_atomically");
    check(bool(hengine.release(*selected)) && bool(hengine.destroy_object(*selected_object)),
        "selected_parts_resources_released");
    const auto channel_object=hengine.create_object(*shape(false,0));
    if(!check(bool(channel_object),"channel_object_created"))return finish();
    const auto channel_program=hengine.prepare(h::parallel(
        h::sequence(anim::move_to(*channel_object,{10,0}).duration(500ms),anim::move_by(*channel_object,{5,0}).duration(500ms)),
        anim::rotate_to(*channel_object,2).duration(2s)));
    if(!check(channel_program && channel_program->segments().size()==1 && channel_program->nodes().size()==5,
        "independent_channels_compile_to_one_object_output_with_retained_author_nodes"))return finish();
    check(channel_program->segments()[0].contributions.size()==3 && channel_program->segments()[0].animation.channels().size()==3,
        "public_plan_exposes_property_channels_and_original_contributions");
    const auto channel_play=hengine.play(*channel_program);
    if(!check(channel_play && hengine.seek(*channel_play,750ms),"independent_channel_program_plays"))return finish();
    const auto inspected_plan=hengine.plan(*channel_play);
    check(inspected_plan && inspected_plan->identity()==channel_program->identity() && inspected_plan->segments()[0].contributions.size()==3,
        "playback_exposes_an_owned_immutable_plan_for_agent_inspection");
    const auto channel_frame=hengine.frame(*channel_play);
    check(channel_frame && channel_frame->geometry.parts[0].local.tx==12.5 && channel_frame->geometry.parts[0].local.rotate==.75,
        "independent_channel_times_are_preserved");
    check(!hengine.inspect(*channel_play)->sample_progress,"multi_channel_plan_has_no_misleading_single_eased_progress");
    const auto channel_pose=copy(*channel_frame);
    check(bool(hengine.retarget(*channel_play,h::PropertyTargets{.offset=h::Vec2{3,1}})) &&
        equal(channel_pose,copy(*hengine.frame(*channel_play))),"retarget_captures_the_complete_composed_pose");
    check(bool(hengine.seek(*channel_play,1s)) && hengine.frame(*channel_play)->geometry.parts[0].local.tx==15.5 &&
        hengine.frame(*channel_play)->geometry.parts[0].local.rotate==.75,"retarget_preserves_other_composed_channels");
    check(bool(hengine.cancel(*channel_play,h::CancelMode::restore)) &&
        equal(channel_pose,copy(*hengine.frame(*channel_play))),"retarget_restore_recovers_the_composed_source");
    check(bool(hengine.release(*channel_play)),"composed_channel_resources_released");
    check(inspected_plan && inspected_plan->nodes().size()==5 && !hengine.plan(*channel_play),
        "retained_plan_survives_retarget_and_release_while_stale_handle_is_refused");
    const auto raw_channel=hanim::PreparedObjectAnimation::property_timeline(*hengine.capture_object(*channel_object),
        std::array{h::PropertyClip{.targets={{.position=h::Vec2{10,0}}},.duration=1s},
                   h::PropertyClip{.targets={{.rotation=2}},.duration=2s}});
    check(raw_channel && raw_channel->channels().size()==2 && raw_channel->timeline_duration()==2s,
        "advanced_property_timeline_is_an_independent_public_library_plan");
    check(h::property_channel_name(h::PropertyChannel::rotation)=="rotation","channel_names_are_stable_public_diagnostics");
    check(bool(hengine.destroy_object(*channel_object)),"channel_object_released");
    const std::array path_verbs{mlibs::geom::PathVerb::move,mlibs::geom::PathVerb::line,
        mlibs::geom::PathVerb::line,mlibs::geom::PathVerb::close};
    std::array path_points{mlibs::geom::Point{0,0},mlibs::geom::Point{12,0},mlibs::geom::Point{0,12}};
    const auto path=h::import_path({path_verbs,path_points});
    if(!check(path && path->contours.size()==1,"neutral_geometry_path_imported_without_renderer"))return finish();
    path_points[1]={99,99};const auto captured=path->contours[0].geometry.view();
    check(captured && captured->segments[0].end==h::Vec2{12,0} &&
        path->contours[0].segment_commands==std::vector<std::size_t>{1,2},"path_capture_owns_geometry_and_preserves_source_indices");
    const auto path_object=hengine.create_object({{{.id="path",.contours={path->contours[0].geometry},.fill=h::Rgba{1,0,0,1}}}});
    if(!check(bool(path_object),"imported_path_creates_public_object"))return finish();
    const auto path_play=hengine.play(anim::move_to(*path_object,{20,10}));bool path_samples=bool(path_play);
    for(int i=0;path_samples && i<=60;++i) {
        path_samples=bool(hengine.seek_progress(*path_play,i/60.));
        const auto frame=hengine.frame(*path_play);
        path_samples=path_samples && frame && frame->geometry.parts[0].local.tx==20*(i/60.) &&
            frame->geometry.cubics.size()==3;
    }
    check(path_samples,"imported_geometry_animates_through_61_public_samples");
    const auto malformed=h::import_path({path_verbs,std::span(path_points).first(1)});
    check(!malformed && malformed.error().path && malformed.error().path->code==mlibs::geom::PathError::missing_points,
        "malformed_path_keeps_shared_source_diagnostic");
    const auto bounded=h::import_path({path_verbs,path_points},{.max_segments=1});
    check(!bounded && bounded.error().code==h::ObjectError::capacity,"path_import_respects_geometry_budget");
    check(path_play && hengine.release(*path_play) && hengine.destroy_object(*path_object),"imported_path_resources_released");
    return finish();
}
