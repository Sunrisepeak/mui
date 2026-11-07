module hanim.program;
import std;

namespace hanim::anim {
namespace {
std::optional<ProgramFailure> invalid_feedback(const RecipeOptions& options,double divisions) {
    const double duration=options.duration.value_or(seconds(.4)).count();
    if(!std::isfinite(duration) || duration<0 ||
       (duration>0 && duration<std::numeric_limits<double>::denorm_min()*divisions))
        return ProgramFailure{ProgramError::invalid_time};
    if(!options.easing.value_or(ease::in_out_sine).valid())return ProgramFailure{ProgramError::invalid_ease};
    return {};
}
Program invalid_target() {
    return invalid_program(ProgramFailure{ProgramError::animation}.because(MorphFailure{MorphError::invalid_property}));
}
// The attention cues' defaults: 0.4 s, each leg in_out_sine.
struct Resolved { seconds duration; Ease easing; std::string part; };
Resolved resolved(const RecipeOptions& options) {
    return {options.duration.value_or(seconds(.4)),options.easing.value_or(ease::in_out_sine),options.part};
}
bool can_double(double value) {
    return std::isfinite(value) && std::abs(value)<=std::numeric_limits<double>::max()/2;
}
bool can_invert(double value) {
    // On the supported binary64 targets this rounded boundary is 2^-1024.
    // Its reciprocal overflows; the next representable magnitude is safe.
    constexpr double floor=1.0/std::numeric_limits<double>::max();
    return std::isfinite(value) && std::abs(value)>floor;
}
}
Program pulse(ObjectInput object,Vec2 factor,RecipeOptions given) {
    const auto options=resolved(given);
    if(auto error=invalid_feedback(given,2))return invalid_program(*error);
    if(!can_invert(factor.x) || !can_invert(factor.y))return invalid_target();
    const auto half=options.duration/2;
    return sequence({
        to(object,{.part=options.part,.scale_factor=factor}).duration(half).easing(options.easing),
        to(object,{.part=options.part,.scale_factor=Vec2{1/factor.x,1/factor.y}})
            .duration(options.duration-half).easing(options.easing)});
}
Program shake(ObjectInput object,Vec2 amplitude,RecipeOptions given) {
    const auto options=resolved(given);
    if(auto error=invalid_feedback(given,4))return invalid_program(*error);
    if(!can_double(amplitude.x) || !can_double(amplitude.y))return invalid_target();
    const auto quarter=options.duration/4,half=options.duration/2;
    return sequence({
        to(object,{.offset=amplitude,.part=options.part}).duration(quarter).easing(options.easing),
        to(object,{.offset=Vec2{-2*amplitude.x,-2*amplitude.y},.part=options.part}).duration(half).easing(options.easing),
        to(object,{.offset=amplitude,.part=options.part}).duration(options.duration-quarter-half).easing(options.easing)});
}
Program wobble(ObjectInput object,double radians,RecipeOptions given) {
    const auto options=resolved(given);
    if(auto error=invalid_feedback(given,4))return invalid_program(*error);
    if(!can_double(radians))return invalid_target();
    const auto quarter=options.duration/4,half=options.duration/2;
    return sequence({
        to(object,{.part=options.part,.rotation_offset=radians}).duration(quarter).easing(options.easing),
        to(object,{.part=options.part,.rotation_offset=-2*radians}).duration(half).easing(options.easing),
        to(object,{.part=options.part,.rotation_offset=radians}).duration(options.duration-quarter-half).easing(options.easing)});
}
}
