module hanim.program;
import std;

namespace hanim::anim {
namespace {
bool subtractable(double a,double b) {
    if(!std::isfinite(a) || !std::isfinite(b))return false;
    constexpr double bound=std::numeric_limits<double>::max();
    return !(b>0 && a< -bound+b) && !(b<0 && a>bound+b);
}
Program rejected(MorphError error,std::size_t point) {
    return invalid_program(ProgramFailure{ProgramError::animation}.because(MorphFailure{.code=error,.where={.clip=point}}));
}
}
Program move_through(ObjectInput object,std::span<const MoveWaypoint> points,MoveThroughOptions options) {
    if(points.empty())return invalid_program({.code=ProgramError::empty_program});
    std::vector<Program> legs;legs.reserve(points.size());
    seconds previous_time{0};Vec2 previous{};
    for(std::size_t i=0;i<points.size();++i) {
        const auto& point=points[i];
        if(!std::isfinite(point.at.count()) || point.at<=previous_time)
            return invalid_program({.code=ProgramError::invalid_time});
        if(!point.easing.valid())return invalid_program({.code=ProgramError::invalid_ease});
        if(!point.path.valid())return rejected(MorphError::invalid_options,i);
        if(!subtractable(point.offset.x,previous.x) || !subtractable(point.offset.y,previous.y))
            return rejected(MorphError::invalid_property,i);
        const Vec2 delta{point.offset.x-previous.x,point.offset.y-previous.y};
        legs.push_back(to(object,{.offset=delta,.part=options.part}).duration(point.at-previous_time)
            .easing(point.easing).with_effect(fx::translation_path(point.path)));
        previous_time=point.at;previous=point.offset;
    }
    return sequence(legs);
}
Program move_through(ObjectInput object,std::initializer_list<MoveWaypoint> points,MoveThroughOptions options) {
    return move_through(std::move(object),std::span<const MoveWaypoint>(points.begin(),points.size()),std::move(options));
}
}
