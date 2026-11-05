module hanim.program;
import std;

namespace hanim::anim {
Program pulse(ObjectInput object,Vec2 factor,FeedbackOptions options) {
    const auto half=options.duration/2;
    return sequence({
        to(object,{.part=options.part,.scale_factor=factor}).duration(half).easing(options.easing),
        to(object,{.part=options.part,.scale_factor=Vec2{1/factor.x,1/factor.y}})
            .duration(options.duration-half).easing(options.easing)});
}
Program shake(ObjectInput object,Vec2 amplitude,FeedbackOptions options) {
    const auto quarter=options.duration/4,half=options.duration/2;
    return sequence({
        to(object,{.offset=amplitude,.part=options.part}).duration(quarter).easing(options.easing),
        to(object,{.offset=Vec2{-2*amplitude.x,-2*amplitude.y},.part=options.part}).duration(half).easing(options.easing),
        to(object,{.offset=amplitude,.part=options.part}).duration(options.duration-quarter-half).easing(options.easing)});
}
Program wobble(ObjectInput object,double radians,FeedbackOptions options) {
    const auto quarter=options.duration/4,half=options.duration/2;
    return sequence({
        to(object,{.part=options.part,.rotation_offset=radians}).duration(quarter).easing(options.easing),
        to(object,{.part=options.part,.rotation_offset=-2*radians}).duration(half).easing(options.easing),
        to(object,{.part=options.part,.rotation_offset=radians}).duration(options.duration-quarter-half).easing(options.easing)});
}
}
